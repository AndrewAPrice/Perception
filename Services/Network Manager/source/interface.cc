// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "interface.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>

#include "address_selection.h"
#include "checksum.h"
#include "endian.h"
#include "ip.h"
#include "ipv6_header.h"
#include "perception/fibers.h"
#include "perception/network_settings.h"
#include "perception/time.h"
#include "protocols.h"

namespace {

using ::perception::AfterDuration;
using ::perception::EnsureInterfaceNetworkSettingsGroup;
using ::perception::Fiber;
using ::perception::FirewallRuleEntry;
using ::perception::FormatInterfaceMacId;
using ::perception::GetCurrentlyExecutingFiber;
using ::perception::GlobalNetworkSettings;
using ::perception::InterfaceAddressMode;
using ::perception::InterfaceAddressStatus;
using ::perception::InterfaceNetworkSettings;
using ::perception::IpsecPolicyEntry;
using ::perception::NetworkConnectStrategy;
using ::perception::NetworkSettingsSubscription;
using ::perception::PublishInterfaceStatus;
using ::perception::ReadGlobalNetworkSettings;
using ::perception::ReadInterfaceNetworkSettings;
using ::perception::Sleep;
using ::perception::StaticRouteEntry;
using ::perception::SubscribeGlobalNetworkSettings;
using ::perception::SubscribeInterfaceNetworkSettings;
using ::perception::devices::MacAddress;
using ::perception::devices::MulticastFilter;
using ::perception::devices::Packet;
using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

// Default static IPv4 address assigned in QEMU slirp (10.0.2.15).
const IpAddress kDefaultIpv4Address = IpAddress::V4(10, 0, 2, 15);

// Default IPv4 on-link subnet prefix (10.0.2.0/24).
const IpAddress kDefaultIpv4Prefix = IpAddress::V4(10, 0, 2, 0);

// Default IPv4 subnet prefix length (/24).
constexpr uint8 kDefaultIpv4PrefixLength = 24;

// Default IPv4 gateway router in QEMU slirp (10.0.2.2).
const IpAddress kDefaultIpv4Gateway = IpAddress::V4(10, 0, 2, 2);

// Default IPv4 DNS server in QEMU slirp (10.0.2.3).
const IpAddress kDefaultIpv4DnsServer = IpAddress::V4(10, 0, 2, 3);

// Default IPv6 DNS server in QEMU slirp (fec0::3).
const IpAddress kDefaultSlirpIpv6Dns = *IpAddress::Parse("fec0::3");

// Default IPv6 prefix in QEMU slirp (fec0::/64).
const IpAddress kDefaultSlirpIpv6Prefix = *IpAddress::Parse("fec0::");

// QEMU slirp default IPv4 gateway MAC (52:55:0a:00:02:02).
constexpr std::array<uint8, 6> kSlirpIpv4GatewayMac = {0x52, 0x55, 0x0a,
                                                       0x00, 0x02, 0x02};

// Number of ARP/NDP resolution attempts before failing.
constexpr int kMaxNeighborProbes = 3;

// Timeout per ARP/NDP probe attempt in milliseconds.
constexpr auto kNeighborProbeTimeout = std::chrono::milliseconds(500);

// Hardware type for Ethernet in ARP packets (1).
constexpr uint16 kArpHtypeEthernet = 1;

// Hardware address length for Ethernet in ARP packets (6).
constexpr uint8 kArpHlenEthernet = 6;

// Protocol address length for IPv4 in ARP packets (4).
constexpr uint8 kArpPlenIpv4 = 4;

// ARP operation code for Request (1).
constexpr uint16 kArpOperRequest = 1;

// ARP operation code for Reply (2).
constexpr uint16 kArpOperReply = 2;

// IPv6 Next Header value for ICMPv6 (58).
constexpr uint8 kProtocolIcmpv6 = 58;

// IPv6 Next Header value for UDP (17).
constexpr uint8 kProtocolUdp = 17;

// Minimum valid IPv6 link MTU (1280, RFC 8200).
constexpr uint32 kMinIpv6Mtu = 1280;

// Per-interface protocol state machines and Registry subscription.
struct InterfaceController {
  size_t iface_idx = 0;
  std::string mac_id;
  InterfaceNetworkSettings settings;
  NetworkSettingsSubscription subscription;
  std::unique_ptr<SlaacController> slaac;
  std::unique_ptr<TemporaryAddressManager> temp_addresses;
  std::unique_ptr<MldManager> mld;
  std::unique_ptr<NudStateMachine> nud;
  std::unique_ptr<RouterAdvertiser> router_advertiser;
  std::unique_ptr<Dhcpv6Client> dhcpv6;
  std::unique_ptr<Dhcpv4Client> dhcpv4;
  std::vector<InterfaceAddressStatus> last_published_addresses;
  std::vector<IpAddress> last_published_routers;
  std::vector<IpAddress> last_published_dns;
};

std::vector<NetworkInterface> interfaces;
std::vector<std::unique_ptr<InterfaceController>> controllers;

GlobalNetworkSettings global_settings;
NetworkSettingsSubscription global_subscription;
bool global_subscription_initialized = false;

RoutingTable routing_table;
Firewall firewall;
PmtuCache pmtu_cache;
Reassembler reassembler;
FragmentIdGenerator fragment_id_generator;
Icmpv6RateLimiter icmpv6_rate_limiter;
IpsecEngine ipsec_engine;

void HandleNeighborResolutionResult(
    size_t iface_idx, const NeighborCache::ResolutionResult& resolved) {
  for (Fiber* waiter : resolved.waiters_to_wake) {
    if (waiter != nullptr) waiter->WakeUp();
  }
  for (const std::string& packet : resolved.pending_packets) {
    uint16 ether_type = kEtherTypeIpv4;
    if (!packet.empty() && (static_cast<uint8>(packet[0]) >> 4) == 6)
      ether_type = kEtherTypeIpv6;
    SendEthernetFrame(iface_idx, resolved.mac, ether_type, packet);
  }
}

void UpdateHardwareMulticastFilter(size_t iface_idx) {
  NetworkInterface* iface = GetInterface(iface_idx);
  if (iface == nullptr) return;

  MulticastFilter req;
  auto add_mac = [&](const std::array<uint8, 6>& m) {
    for (const auto& existing : req.addresses) {
      if (std::memcmp(existing.mac, m.data(), 6) == 0) return;
    }
    MacAddress entry{};
    std::memcpy(entry.mac, m.data(), 6);
    req.addresses.push_back(entry);
  };

  if (auto all_nodes = MulticastMac(AllNodesMulticastAddress());
      all_nodes.has_value()) {
    add_mac(*all_nodes);
  }
  for (const auto& [group, refcount] : iface->multicast_groups) {
    if (refcount <= 0) continue;
    if (auto mac = MulticastMac(group); mac.has_value()) add_mac(*mac);
  }
  (void)iface->device.SetMulticastFilter(req);
}

void JoinOrLeaveMulticastGroup(size_t iface_idx, const IpAddress& group,
                               bool joined,
                               std::chrono::steady_clock::time_point now) {
  NetworkInterface* iface = GetInterface(iface_idx);
  if (iface == nullptr || iface_idx >= controllers.size()) return;
  auto& ctrl = controllers[iface_idx];
  int& count = iface->multicast_groups[group];
  if (joined) {
    const bool first_join = (count <= 0);
    ++count;
    if (first_join) {
      UpdateHardwareMulticastFilter(iface_idx);
      if (ctrl->mld) ctrl->mld->JoinGroup(group, now);
    }
  } else if (count > 0) {
    --count;
    if (count == 0) {
      iface->multicast_groups.erase(group);
      UpdateHardwareMulticastFilter(iface_idx);
      if (ctrl->mld) ctrl->mld->LeaveGroup(group, now);
    }
  }
}

std::string FormatAddressOrigin(AddressOrigin origin) {
  switch (origin) {
    case AddressOrigin::Static:
      return "static";
    case AddressOrigin::LinkLocal:
      return "link-local";
    case AddressOrigin::Slaac:
      return "slaac";
    case AddressOrigin::Dhcpv6:
      return "dhcpv6";
    case AddressOrigin::Temporary:
      return "temporary";
    case AddressOrigin::Dhcpv4:
      return "dhcpv4";
  }
  return "static";
}

std::string FormatAddressState(AddressState state) {
  switch (state) {
    case AddressState::Tentative:
      return "tentative";
    case AddressState::Preferred:
      return "preferred";
    case AddressState::Deprecated:
      return "deprecated";
    case AddressState::Duplicate:
      return "duplicate";
  }
  return "preferred";
}

void RebuildRoutingTable(std::chrono::steady_clock::time_point now) {
  routing_table.PurgeExpired(now);
  for (size_t idx = 0; idx < interfaces.size(); ++idx) {
    routing_table.RemoveRoutesForInterface(idx);
    const NetworkInterface& iface = interfaces[idx];
    for (const InterfaceAddress& prefix : iface.on_link_prefixes) {
      routing_table.AddConnectedRoute(prefix.address, prefix.prefix_length,
                                      idx);
    }
    for (const InterfaceAddress& addr : iface.addresses) {
      if (addr.state == AddressState::Duplicate) continue;
      if (addr.prefix_length > 0 && addr.prefix_length < 128) {
        routing_table.AddConnectedRoute(
            MaskPrefix(addr.address, addr.prefix_length), addr.prefix_length,
            idx);
      }
    }
    for (const DefaultRouter& router : iface.default_routers) {
      if (now >= router.valid_until) continue;
      const IpAddress default_prefix =
          router.address.IsV6() ? IpAddress::V6Any() : IpAddress::V4Any();
      if (router.valid_until == std::chrono::steady_clock::time_point::max()) {
        routing_table.AddStaticRoute(default_prefix, 0, router.address, idx);
      } else {
        routing_table.AddRaRoute(default_prefix, 0, router.address, idx,
                                 router.valid_until);
      }
    }
  }
  for (const StaticRouteEntry& sr : global_settings.static_routes) {
    size_t target_idx = 0;
    for (size_t i = 0; i < controllers.size(); ++i) {
      if (controllers[i] && controllers[i]->mac_id == sr.interface_id) {
        target_idx = i;
        break;
      }
    }
    if (target_idx < interfaces.size()) {
      routing_table.AddStaticRoute(sr.destination, sr.prefix_length,
                                   sr.next_hop, target_idx);
    }
  }
}

void SyncAndPublishInterface(size_t iface_idx,
                             std::chrono::steady_clock::time_point now) {
  NetworkInterface* iface = GetInterface(iface_idx);
  if (iface == nullptr || iface_idx >= controllers.size() ||
      !controllers[iface_idx])
    return;
  InterfaceController& ctrl = *controllers[iface_idx];

  std::vector<InterfaceAddress> merged;
  if (ctrl.settings.ipv4_mode == InterfaceAddressMode::kStatic) {
    for (const auto& sa : ctrl.settings.ipv4_static_addresses) {
      if (!sa.address.IsV4()) continue;
      InterfaceAddress entry;
      entry.address = sa.address;
      entry.prefix_length = sa.prefix_length;
      entry.state = AddressState::Preferred;
      entry.origin = AddressOrigin::Static;
      merged.push_back(entry);
    }
  } else if (ctrl.settings.ipv4_mode == InterfaceAddressMode::kAutomatic) {
    if (ctrl.dhcpv4 && ctrl.dhcpv4->address().has_value() &&
        (ctrl.dhcpv4->address()->state == AddressState::Preferred ||
         ctrl.dhcpv4->address()->state == AddressState::Deprecated)) {
      merged.push_back(*ctrl.dhcpv4->address());
    } else {
      InterfaceAddress fallback_v4;
      fallback_v4.address = kDefaultIpv4Address;
      fallback_v4.prefix_length = kDefaultIpv4PrefixLength;
      fallback_v4.state = AddressState::Preferred;
      fallback_v4.origin = AddressOrigin::Dhcpv4;
      merged.push_back(fallback_v4);
    }
  }

  if (ctrl.settings.ipv6_mode != InterfaceAddressMode::kDisabled) {
    for (const auto& sa : ctrl.settings.ipv6_static_addresses) {
      if (!sa.address.IsV6()) continue;
      InterfaceAddress entry;
      entry.address = sa.address;
      entry.prefix_length = sa.prefix_length;
      entry.state = AddressState::Preferred;
      entry.origin = AddressOrigin::Static;
      merged.push_back(entry);
    }
    if (ctrl.slaac) {
      for (const auto& addr : ctrl.slaac->addresses()) merged.push_back(addr);
    }
    if (ctrl.temp_addresses) {
      for (const auto& addr : ctrl.temp_addresses->addresses())
        merged.push_back(addr);
    }
    if (ctrl.dhcpv6) {
      for (const auto& addr : ctrl.dhcpv6->addresses()) merged.push_back(addr);
    }
  }
  iface->addresses = std::move(merged);

  if (ctrl.dhcpv4 && ctrl.dhcpv4->default_router().has_value()) {
    const IpAddress gw = *ctrl.dhcpv4->default_router();
    bool found = false;
    for (auto& r : iface->default_routers) {
      if (r.address == gw) {
        found = true;
        break;
      }
    }
    if (!found) iface->default_routers.push_back({gw});
  }
  if (ctrl.dhcpv6) {
    for (const IpAddress& dns : ctrl.dhcpv6->dns_servers()) {
      bool found = false;
      for (const auto& existing : iface->dns_servers) {
        if (existing.address == dns) {
          found = true;
          break;
        }
      }
      if (!found) iface->dns_servers.push_back({dns});
    }
  }

  RebuildRoutingTable(now);

  std::vector<InterfaceAddressStatus> status_addrs;
  for (const InterfaceAddress& addr : iface->addresses) {
    InterfaceAddressStatus st;
    st.address = addr.address;
    st.prefix_length = addr.prefix_length;
    st.origin = FormatAddressOrigin(addr.origin);
    st.state = FormatAddressState(addr.state);
    if (addr.valid_until == std::chrono::steady_clock::time_point::max()) {
      st.expires = "forever";
    } else if (addr.valid_until <= now) {
      st.expires = "0s";
    } else {
      auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                      addr.valid_until - now)
                      .count();
      st.expires = std::to_string(secs) + "s";
    }
    status_addrs.push_back(std::move(st));
  }

  std::vector<IpAddress> status_routers;
  for (const DefaultRouter& r : iface->default_routers) {
    if (now < r.valid_until) status_routers.push_back(r.address);
  }
  std::vector<IpAddress> status_dns;
  for (const DnsServer& d : iface->dns_servers) {
    if (now < d.valid_until) status_dns.push_back(d.address);
  }

  if (status_addrs != ctrl.last_published_addresses ||
      status_routers != ctrl.last_published_routers ||
      status_dns != ctrl.last_published_dns) {
    ctrl.last_published_addresses = status_addrs;
    ctrl.last_published_routers = status_routers;
    ctrl.last_published_dns = status_dns;
    (void)PublishInterfaceStatus(ctrl.mac_id, status_addrs, status_routers,
                                 status_dns);
  }
}

void InstallDefaultFirewallRules() {
  firewall.ClearRules();
  // Always permit inbound DHCPv4 client replies (UDP 67 -> 68), DHCPv6 client
  // replies (UDP 547 -> 546), and DNS replies (UDP src 53).
  FirewallRule dhcp4_rule;
  dhcp4_rule.action = FirewallAction::Allow;
  dhcp4_rule.direction = FirewallDirection::Inbound;
  dhcp4_rule.family = IpAddressFamily::V4;
  dhcp4_rule.protocol = kProtocolUdp;
  dhcp4_rule.src_port_range = {kDhcpv4ServerPort, kDhcpv4ServerPort};
  dhcp4_rule.dst_port_range = {kDhcpv4ClientPort, kDhcpv4ClientPort};
  firewall.AddRule(dhcp4_rule);

  FirewallRule dhcp6_rule;
  dhcp6_rule.action = FirewallAction::Allow;
  dhcp6_rule.direction = FirewallDirection::Inbound;
  dhcp6_rule.family = IpAddressFamily::V6;
  dhcp6_rule.protocol = kProtocolUdp;
  dhcp6_rule.src_port_range = {kDhcpv6ServerPort, kDhcpv6ServerPort};
  dhcp6_rule.dst_port_range = {kDhcpv6ClientPort, kDhcpv6ClientPort};
  firewall.AddRule(dhcp6_rule);

  FirewallRule dns_rule;
  dns_rule.action = FirewallAction::Allow;
  dns_rule.direction = FirewallDirection::Inbound;
  dns_rule.protocol = kProtocolUdp;
  dns_rule.src_port_range = {53, 53};
  firewall.AddRule(dns_rule);
}

void ApplyGlobalNetworkSettings() {
  global_settings = ReadGlobalNetworkSettings();

  for (auto& ctrl : controllers) {
    if (ctrl && ctrl->temp_addresses) {
      ctrl->temp_addresses->SetEnabled(
          global_settings.enable_temporary_addresses);
    }
  }

  InstallDefaultFirewallRules();

  for (const FirewallRuleEntry& entry : global_settings.firewall_rules) {
    FirewallRule rule;
    rule.action = (entry.action == "allow" || entry.action == "Allow")
                      ? FirewallAction::Allow
                      : FirewallAction::Deny;
    if (entry.direction == "inbound" || entry.direction == "Inbound") {
      rule.direction = FirewallDirection::Inbound;
    } else if (entry.direction == "outbound" || entry.direction == "Outbound") {
      rule.direction = FirewallDirection::Outbound;
    } else {
      rule.direction = FirewallDirection::Any;
    }
    if (entry.protocol == "tcp" || entry.protocol == "TCP") {
      rule.protocol = 6;
    } else if (entry.protocol == "udp" || entry.protocol == "UDP") {
      rule.protocol = 17;
    } else if (entry.protocol == "icmp" || entry.protocol == "ICMP") {
      rule.protocol = 1;
    } else if (entry.protocol == "icmpv6" || entry.protocol == "ICMPv6") {
      rule.protocol = 58;
    }
    if (!entry.port.empty()) {
      int port_num = std::atoi(entry.port.c_str());
      if (port_num > 0 && port_num <= 65535) {
        rule.dst_port_range = {static_cast<uint16>(port_num),
                               static_cast<uint16>(port_num)};
      }
    }
    firewall.AddRule(rule);
  }

  ipsec_engine.ClearSpd();
  for (const IpsecPolicyEntry& pol : global_settings.ipsec_policies) {
    if (pol.peer_address.IsUnspecified() || pol.spi == 0) continue;
    const IpsecMode mode = (pol.mode == "tunnel" || pol.mode == "Tunnel")
                               ? IpsecMode::Tunnel
                               : IpsecMode::Transport;
    SecurityAssociation sa;
    sa.spi = pol.spi;
    sa.mode = mode;
    sa.cipher = EspCipherSuite::ChaCha20Poly1305;
    sa.tunnel_remote = pol.peer_address;
    auto hex_nibble = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
      if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
      return -1;
    };
    size_t byte_idx = 0;
    for (size_t i = 0; i + 1 < pol.key_hex.size() && byte_idx < 36; i += 2) {
      int hi = hex_nibble(pol.key_hex[i]);
      int lo = hex_nibble(pol.key_hex[i + 1]);
      if (hi < 0 || lo < 0) break;
      uint8 val = static_cast<uint8>((hi << 4) | lo);
      if (byte_idx < 32) {
        sa.key[byte_idx] = val;
      } else {
        sa.salt[byte_idx - 32] = val;
      }
      ++byte_idx;
    }
    ipsec_engine.InstallSa(sa);

    SpdRule out_rule;
    out_rule.family = pol.peer_address.family();
    out_rule.dst_prefix = pol.peer_address;
    out_rule.dst_prefix_length = pol.peer_address.IsV6() ? 128 : 32;
    out_rule.action = SpdAction::Protect;
    out_rule.mode = mode;
    out_rule.sa_spi = pol.spi;
    ipsec_engine.AddSpdRule(out_rule);
  }
  SpdRule default_bypass;
  default_bypass.action = SpdAction::Bypass;
  ipsec_engine.AddSpdRule(default_bypass);

  RebuildRoutingTable(std::chrono::steady_clock::now());
}

}  // namespace

std::optional<IpAddress> NetworkInterface::GetPreferredAddress(
    IpAddressFamily family) const {
  std::optional<IpAddress> fallback;
  for (const InterfaceAddress& entry : addresses) {
    if (entry.address.family() != family) continue;
    if (entry.state == AddressState::Preferred && !entry.address.IsLinkLocal())
      return entry.address;
    if (entry.state == AddressState::Preferred && !fallback.has_value())
      fallback = entry.address;
    if (entry.state == AddressState::Deprecated && !fallback.has_value())
      fallback = entry.address;
  }
  return fallback;
}

bool NetworkInterface::HasAddress(const IpAddress& address) const {
  for (const InterfaceAddress& entry : addresses) {
    if (entry.state != AddressState::Duplicate && entry.address == address)
      return true;
  }
  return false;
}

bool NetworkInterface::IsOnLink(const IpAddress& destination) const {
  if (destination.IsUnspecified()) return false;
  if (destination.IsLinkLocal()) return true;
  for (const InterfaceAddress& prefix : on_link_prefixes) {
    if (prefix.address.family() == destination.family() &&
        destination.IsInPrefix(prefix.address, prefix.prefix_length)) {
      return true;
    }
  }
  for (const InterfaceAddress& addr : addresses) {
    if (addr.address.family() == destination.family() &&
        addr.state != AddressState::Duplicate && addr.prefix_length > 0 &&
        addr.prefix_length < 128 &&
        destination.IsInPrefix(addr.address, addr.prefix_length)) {
      return true;
    }
  }
  return false;
}

std::optional<IpAddress> NetworkInterface::SelectNextHop(
    const IpAddress& destination) const {
  if (destination.IsUnspecified()) return std::nullopt;
  if (destination.IsBroadcast() || destination.IsMulticast() ||
      IsOnLink(destination)) {
    return destination;
  }

  auto now = std::chrono::steady_clock::now();
  std::optional<IpAddress> fallback_router;
  for (const DefaultRouter& router : default_routers) {
    if (router.address.family() != destination.family()) continue;
    if (now >= router.valid_until) continue;
    const NeighborEntry* neighbor = neighbor_cache.Find(router.address);
    if (neighbor != nullptr && neighbor->state == NeighborState::Reachable)
      return router.address;
    if (!fallback_router.has_value()) fallback_router = router.address;
  }
  return fallback_router;
}

bool NetworkInterface::IsDestinedForMac(
    const std::array<uint8, 6>& dest_mac) const {
  if (dest_mac == mac || IsBroadcastMac(dest_mac)) return true;
  if (!IsMulticastMac(dest_mac)) return false;
  if (dest_mac[0] == 0x01 && dest_mac[1] == 0x00 && dest_mac[2] == 0x5E)
    return true;
  if (dest_mac[0] == 0x33 && dest_mac[1] == 0x33) {
    if (dest_mac[2] == 0x00 && dest_mac[3] == 0x00 && dest_mac[4] == 0x00 &&
        dest_mac[5] == 0x01) {
      return true;
    }
    for (const auto& [group, refcount] : multicast_groups) {
      if (refcount <= 0) continue;
      auto group_mac = MulticastMac(group);
      if (group_mac.has_value() && *group_mac == dest_mac) return true;
    }
  }
  return false;
}

const std::vector<NetworkInterface>& GetNetworkInterfaces() {
  return interfaces;
}

NetworkInterface& GetNetworkInterface(size_t index) {
  return interfaces[index];
}

NetworkInterface* GetInterface(size_t index) {
  if (index >= interfaces.size()) return nullptr;
  return &interfaces[index];
}

size_t GetNetworkInterfaceCount() { return interfaces.size(); }

size_t AddNetworkInterface(NetworkInterface iface) {
  if (!global_subscription_initialized) {
    global_subscription_initialized = true;
    ApplyGlobalNetworkSettings();
    global_subscription =
        SubscribeGlobalNetworkSettings([]() { ApplyGlobalNetworkSettings(); });
  }

  if (iface.addresses.empty()) {
    InterfaceAddress fallback_v4;
    fallback_v4.address = kDefaultIpv4Address;
    fallback_v4.prefix_length = kDefaultIpv4PrefixLength;
    fallback_v4.state = AddressState::Preferred;
    fallback_v4.origin = AddressOrigin::Dhcpv4;
    iface.addresses.push_back(fallback_v4);
  }
  if (iface.on_link_prefixes.empty()) {
    InterfaceAddress v4_prefix;
    v4_prefix.address = kDefaultIpv4Prefix;
    v4_prefix.prefix_length = kDefaultIpv4PrefixLength;
    v4_prefix.state = AddressState::Preferred;
    v4_prefix.origin = AddressOrigin::Static;
    iface.on_link_prefixes.push_back(v4_prefix);
  }
  if (iface.default_routers.empty()) {
    DefaultRouter v4_router;
    v4_router.address = kDefaultIpv4Gateway;
    iface.default_routers.push_back(v4_router);
  }
  if (iface.dns_servers.empty()) {
    DnsServer v4_dns;
    v4_dns.address = kDefaultIpv4DnsServer;
    iface.dns_servers.push_back(v4_dns);
  }

  const size_t idx = interfaces.size();
  const HardwareAddress mac = iface.mac;

  auto ctrl = std::make_unique<InterfaceController>();
  ctrl->iface_idx = idx;
  ctrl->mac_id = FormatInterfaceMacId(mac);
  (void)EnsureInterfaceNetworkSettingsGroup(ctrl->mac_id);
  ctrl->settings = ReadInterfaceNetworkSettings(ctrl->mac_id);

  auto send_v6_sink = [idx](Ipv6Datagram dg) {
    std::string raw = SerializeIpv6Datagram(dg);
    if (dg.destination.IsMulticast()) {
      if (auto dst_mac = MulticastMac(dg.destination); dst_mac.has_value())
        SendEthernetFrame(idx, *dst_mac, kEtherTypeIpv6, raw);
    } else {
      (void)SendRawIpPacket(idx, dg.destination, raw);
    }
  };

  ctrl->mld = std::make_unique<MldManager>(send_v6_sink);
  ctrl->mld->SetLinkLocalSource(LinkLocalAddressFromMac(mac));

  ctrl->slaac = std::make_unique<SlaacController>(mac, send_v6_sink);

  ctrl->temp_addresses = std::make_unique<TemporaryAddressManager>(
      mac,
      [idx](const IpAddress& target) {
        SendNdpNeighborSolicitation(idx, target, /*is_dad=*/true);
      },
      [idx](const IpAddress& group, bool joined) {
        JoinOrLeaveMulticastGroup(idx, group, joined,
                                  std::chrono::steady_clock::now());
      },
      TemporaryIidRandomFn{}, global_settings.enable_temporary_addresses);

  ctrl->nud = std::make_unique<NudStateMachine>(LinkLocalAddressFromMac(mac),
                                                mac, send_v6_sink);

  ctrl->router_advertiser = std::make_unique<RouterAdvertiser>(send_v6_sink);

  ctrl->dhcpv6 = std::make_unique<Dhcpv6Client>(
      mac, [idx](std::string payload) {
        NetworkInterface* nic = GetInterface(idx);
        if (nic == nullptr) return;
        const IpAddress src = LinkLocalAddressFromMac(nic->mac);
        const IpAddress dst = *IpAddress::Parse("ff02::1:2");
        UdpHeader udp{};
        const uint16 udp_len =
            static_cast<uint16>(sizeof(UdpHeader) + payload.size());
        udp.src_port = Swap16BitEndian(kDhcpv6ClientPort);
        udp.dest_port = Swap16BitEndian(kDhcpv6ServerPort);
        udp.length = Swap16BitEndian(udp_len);
        udp.checksum = 0;
        std::string segment(reinterpret_cast<const char*>(&udp), sizeof(udp));
        segment.append(payload);
        uint16 csum =
            Swap16BitEndian(TransportChecksum(src, dst, kProtocolUdp, segment));
        if (csum == 0) csum = 0xFFFF;
        std::memcpy(segment.data() + 6, &csum, 2);
        if (auto dst_mac = MulticastMac(dst); dst_mac.has_value()) {
          Ipv6Datagram dg;
          dg.source = src;
          dg.destination = dst;
          dg.next_header = kProtocolUdp;
          dg.hop_limit = nic->ipv6_hop_limit;
          dg.payload = std::move(segment);
          SendEthernetFrame(idx, *dst_mac, kEtherTypeIpv6,
                            SerializeIpv6Datagram(dg));
        }
      });

  ctrl->dhcpv4 = std::make_unique<Dhcpv4Client>(
      mac,
      [idx](std::string payload, const IpAddress& src_ip,
            const IpAddress& dst_ip) {
        UdpHeader udp{};
        const uint16 udp_len =
            static_cast<uint16>(sizeof(UdpHeader) + payload.size());
        udp.src_port = Swap16BitEndian(kDhcpv4ClientPort);
        udp.dest_port = Swap16BitEndian(kDhcpv4ServerPort);
        udp.length = Swap16BitEndian(udp_len);
        udp.checksum = 0;
        std::string segment(reinterpret_cast<const char*>(&udp), sizeof(udp));
        segment.append(payload);
        uint16 csum = Swap16BitEndian(
            TransportChecksum(src_ip, dst_ip, kProtocolUdp, segment));
        if (csum == 0) csum = 0xFFFF;
        std::memcpy(segment.data() + 6, &csum, 2);
        std::string ipv4_pkt = BuildIpv4Packet(src_ip, dst_ip, kProtocolUdp,
                                               segment, 64, /*dont_fragment=*/false);
        SendEthernetFrame(idx, kBroadcastMac, kEtherTypeIpv4, ipv4_pkt);
      },
      [idx](const IpAddress& sender_ip, const IpAddress& target_ip) {
        NetworkInterface* nic = GetInterface(idx);
        if (nic == nullptr) return;
        ArpHeader arp{};
        arp.htype = Swap16BitEndian(kArpHtypeEthernet);
        arp.ptype = Swap16BitEndian(kEtherTypeIpv4);
        arp.hlen = kArpHlenEthernet;
        arp.plen = kArpPlenIpv4;
        arp.oper = Swap16BitEndian(kArpOperRequest);
        std::memcpy(arp.sha, nic->mac.data(), 6);
        sender_ip.CopyTo(arp.spa);
        std::memset(arp.tha, 0x00, 6);
        target_ip.CopyTo(arp.tpa);
        SendEthernetFrame(
            idx, kBroadcastMac, kEtherTypeArp,
            std::string_view(reinterpret_cast<const char*>(&arp), sizeof(arp)));
      });

  RouterAdvertisementConfig ra_cfg;
  ra_cfg.enabled =
      global_settings.ipv6_forwarding && ctrl->settings.ipv6_router_mode;
  ra_cfg.link_local_source = LinkLocalAddressFromMac(mac);
  ra_cfg.source_mac = mac;
  ctrl->router_advertiser->SetConfig(ra_cfg, std::chrono::steady_clock::now());

  const std::string mac_id = ctrl->mac_id;
  ctrl->subscription = SubscribeInterfaceNetworkSettings(mac_id, [idx]() {
    if (idx >= controllers.size() || !controllers[idx]) return;
    auto now = std::chrono::steady_clock::now();
    controllers[idx]->settings =
        ReadInterfaceNetworkSettings(controllers[idx]->mac_id);
    SyncAndPublishInterface(idx, now);
  });

  interfaces.push_back(std::move(iface));
  controllers.push_back(std::move(ctrl));
  RebuildRoutingTable(std::chrono::steady_clock::now());

  const auto now = std::chrono::steady_clock::now();
  JoinOrLeaveMulticastGroup(idx, AllNodesMulticastAddress(), true, now);
  JoinOrLeaveMulticastGroup(
      idx, SolicitedNodeMulticastAddress(LinkLocalAddressFromMac(mac)), true,
      now);
  if (controllers[idx]->settings.ipv6_mode == InterfaceAddressMode::kAutomatic)
    controllers[idx]->slaac->BringUpLinkLocal(now);
  if (controllers[idx]->settings.ipv4_mode == InterfaceAddressMode::kAutomatic)
    controllers[idx]->dhcpv4->Start(now);

  SyncAndPublishInterface(idx, now);
  SendArpRequest(idx, kDefaultIpv4Gateway);
  SendArpRequest(idx, kDefaultIpv4DnsServer);
  return idx;
}

std::optional<size_t> SelectInterfaceForDestination(const IpAddress& dst) {
  if (dst.IsUnspecified() || interfaces.empty()) return std::nullopt;
  if (auto resolved =
          routing_table.Lookup(dst, std::chrono::steady_clock::now());
      resolved.has_value() && resolved->interface_index < interfaces.size()) {
    return resolved->interface_index;
  }
  for (size_t i = 0; i < interfaces.size(); ++i) {
    if (interfaces[i].IsOnLink(dst)) return i;
  }
  for (size_t i = 0; i < interfaces.size(); ++i) {
    if (interfaces[i].SelectNextHop(dst).has_value()) return i;
  }
  return 0;
}

std::optional<IpAddress> SelectSourceAddress(size_t iface_idx,
                                             const IpAddress& dst) {
  NetworkInterface* iface = GetInterface(iface_idx);
  if (iface == nullptr) return std::nullopt;

  std::vector<SourceAddressCandidate> candidates;
  for (const InterfaceAddress& entry : iface->addresses) {
    if (entry.address.family() != dst.family()) continue;
    if (entry.state != AddressState::Preferred &&
        entry.state != AddressState::Deprecated)
      continue;
    SourceAddressCandidate cand;
    cand.address = entry.address;
    cand.prefix_length = entry.prefix_length;
    cand.deprecated = (entry.state == AddressState::Deprecated);
    cand.temporary = (entry.origin == AddressOrigin::Temporary);
    cand.on_outgoing_interface = true;
    candidates.push_back(cand);
  }

  AddressSelectionOptions opts = DefaultAddressSelectionOptions();
  opts.prefer_ipv4 = global_settings.prefer_ipv4;
  opts.prefer_temporary_addresses = global_settings.enable_temporary_addresses;
  AddressSelector selector(opts);
  if (auto selected = selector.SelectSource(dst, candidates);
      selected.has_value()) {
    return selected->address;
  }
  return iface->GetPreferredAddress(dst.family());
}

std::optional<IpAddress> SelectNextHop(size_t iface_idx, const IpAddress& dst) {
  NetworkInterface* iface = GetInterface(iface_idx);
  if (iface == nullptr) return std::nullopt;
  if (auto resolved =
          routing_table.Lookup(dst, std::chrono::steady_clock::now());
      resolved.has_value() && resolved->interface_index == iface_idx) {
    return resolved->immediate_next_hop;
  }
  return iface->SelectNextHop(dst);
}

bool SendEthernetFrame(size_t iface_idx, const std::array<uint8, 6>& dest_mac,
                       uint16 ether_type, std::string_view payload) {
  NetworkInterface* iface = GetInterface(iface_idx);
  if (iface == nullptr) return false;

  Packet pkt;
  pkt.data = BuildEthernetFrame(iface->mac, dest_mac, ether_type, payload);
  return iface->device.SendPacket(pkt) == ::Status::OK;
}

std::optional<std::array<uint8, 6>> ResolveNeighbor(
    size_t iface_idx, const IpAddress& next_hop) {
  NetworkInterface* iface = GetInterface(iface_idx);
  if (iface == nullptr) return std::nullopt;

  if (next_hop.IsBroadcast()) return kBroadcastMac;
  if (next_hop.IsMulticast()) return MulticastMac(next_hop);

  if (auto mac = iface->neighbor_cache.LookupMac(next_hop); mac.has_value())
    return mac;

  for (int attempt = 0; attempt < kMaxNeighborProbes; attempt++) {
    iface = GetInterface(iface_idx);
    if (iface == nullptr) return std::nullopt;

    if (auto mac = iface->neighbor_cache.LookupMac(next_hop); mac.has_value())
      return mac;

    if (next_hop.IsV4()) {
      SendArpRequest(iface_idx, next_hop);
    } else if (next_hop.IsV6()) {
      SendNdpNeighborSolicitation(iface_idx, next_hop, /*is_dad=*/false);
    }

    iface = GetInterface(iface_idx);
    if (iface == nullptr) return std::nullopt;
    if (auto mac = iface->neighbor_cache.LookupMac(next_hop); mac.has_value())
      return mac;

    Fiber* self = GetCurrentlyExecutingFiber();
    if (self == nullptr) break;

    iface->neighbor_cache.AddWaiter(next_hop, self);
    auto finished = std::make_shared<bool>(false);
    AfterDuration(kNeighborProbeTimeout, [finished, self]() {
      if (!*finished) {
        *finished = true;
        self->WakeUp();
      }
    });

    Sleep();
    *finished = true;

    iface = GetInterface(iface_idx);
    if (iface == nullptr) return std::nullopt;
    iface->neighbor_cache.RemoveWaiter(next_hop, self);

    if (auto mac = iface->neighbor_cache.LookupMac(next_hop); mac.has_value())
      return mac;
  }

  // Fallback for QEMU slirp virtual hosts on 10.0.2.0/24 if ARP did not
  // complete in time.
  if (next_hop.IsV4() &&
      next_hop.IsInPrefix(kDefaultIpv4Prefix, kDefaultIpv4PrefixLength)) {
    RecordNeighborReachable(iface_idx, next_hop, kSlirpIpv4GatewayMac);
    return kSlirpIpv4GatewayMac;
  }
  return std::nullopt;
}

bool ResolveAndSendFrame(size_t iface_idx, const IpAddress& next_hop,
                         uint16 ether_type, std::string_view payload) {
  auto mac = ResolveNeighbor(iface_idx, next_hop);
  if (!mac.has_value()) return false;
  return SendEthernetFrame(iface_idx, *mac, ether_type, payload);
}

void RecordNeighborReachable(size_t iface_idx, const IpAddress& ip,
                             const std::array<uint8, 6>& mac) {
  NetworkInterface* iface = GetInterface(iface_idx);
  if (iface == nullptr) return;
  auto resolved = iface->neighbor_cache.UpdateReachable(ip, mac);
  if (ip.IsV6() && iface_idx < controllers.size() && controllers[iface_idx] &&
      controllers[iface_idx]->nud) {
    NdpNeighborAdvertisement na;
    na.solicited_flag = true;
    na.override_flag = true;
    na.target = ip;
    na.target_mac = mac;
    controllers[iface_idx]->nud->OnNeighborAdvertisement(
        na, std::chrono::steady_clock::now());
  }
  HandleNeighborResolutionResult(iface_idx, resolved);
}

void RecordUnsolicitedNeighbor(size_t iface_idx, const IpAddress& ip,
                               const std::array<uint8, 6>& mac,
                               bool create_if_missing) {
  NetworkInterface* iface = GetInterface(iface_idx);
  if (iface == nullptr) return;
  auto resolved =
      iface->neighbor_cache.RecordUnsolicitedAddress(ip, mac, create_if_missing);
  if (ip.IsV6() && iface_idx < controllers.size() && controllers[iface_idx] &&
      controllers[iface_idx]->nud) {
    controllers[iface_idx]->nud->RecordPassiveLinkLayerAddress(
        ip, mac, /*is_router=*/false, std::chrono::steady_clock::now());
  }
  HandleNeighborResolutionResult(iface_idx, resolved);
}

void SendArpRequest(size_t iface_idx, const IpAddress& target_ip) {
  if (!target_ip.IsV4()) return;
  NetworkInterface* iface = GetInterface(iface_idx);
  if (iface == nullptr) return;

  auto src_ip = iface->GetPreferredAddress(IpAddressFamily::V4);
  if (!src_ip.has_value()) return;

  ArpHeader arp{};
  arp.htype = Swap16BitEndian(kArpHtypeEthernet);
  arp.ptype = Swap16BitEndian(kEtherTypeIpv4);
  arp.hlen = kArpHlenEthernet;
  arp.plen = kArpPlenIpv4;
  arp.oper = Swap16BitEndian(kArpOperRequest);
  std::memcpy(arp.sha, iface->mac.data(), 6);
  src_ip->CopyTo(arp.spa);
  std::memset(arp.tha, 0x00, 6);
  target_ip.CopyTo(arp.tpa);

  SendEthernetFrame(
      iface_idx, kBroadcastMac, kEtherTypeArp,
      std::string_view(reinterpret_cast<const char*>(&arp), sizeof(arp)));
}

void SendArpReply(size_t iface_idx, const std::array<uint8, 6>& target_mac,
                  const IpAddress& target_ip) {
  if (!target_ip.IsV4()) return;
  NetworkInterface* iface = GetInterface(iface_idx);
  if (iface == nullptr) return;

  auto src_ip = iface->GetPreferredAddress(IpAddressFamily::V4);
  if (!src_ip.has_value()) return;

  ArpHeader arp{};
  arp.htype = Swap16BitEndian(kArpHtypeEthernet);
  arp.ptype = Swap16BitEndian(kEtherTypeIpv4);
  arp.hlen = kArpHlenEthernet;
  arp.plen = kArpPlenIpv4;
  arp.oper = Swap16BitEndian(kArpOperReply);
  std::memcpy(arp.sha, iface->mac.data(), 6);
  src_ip->CopyTo(arp.spa);
  std::memcpy(arp.tha, target_mac.data(), 6);
  target_ip.CopyTo(arp.tpa);

  SendEthernetFrame(
      iface_idx, target_mac, kEtherTypeArp,
      std::string_view(reinterpret_cast<const char*>(&arp), sizeof(arp)));
}

void SendNdpNeighborSolicitation(size_t iface_idx, const IpAddress& target_ip,
                                 bool is_dad) {
  if (!target_ip.IsV6()) return;
  NetworkInterface* iface = GetInterface(iface_idx);
  if (iface == nullptr) return;

  const IpAddress dst = SolicitedNodeMulticastAddress(target_ip);
  auto dst_mac = MulticastMac(dst);
  if (!dst_mac.has_value()) return;

  IpAddress src = IpAddress::V6Any();
  std::optional<HardwareAddress> sllao;
  if (!is_dad) {
    src = SelectSourceAddress(iface_idx, target_ip)
              .value_or(LinkLocalAddressFromMac(iface->mac));
    sllao = iface->mac;
  }

  NdpNeighborSolicitation ns;
  ns.target = target_ip;
  ns.source_mac = sllao;
  std::string icmp_pkt = BuildNeighborSolicitation(src, dst, ns);

  Ipv6Datagram dg;
  dg.source = src;
  dg.destination = dst;
  dg.next_header = kProtocolIcmpv6;
  dg.hop_limit = kNdpHopLimit;
  dg.payload = std::move(icmp_pkt);
  SendEthernetFrame(iface_idx, *dst_mac, kEtherTypeIpv6,
                    SerializeIpv6Datagram(dg));
}

uint16 GetEffectivePathMtu(const IpAddress& dst) {
  uint16 link_mtu = 1500;
  if (auto idx = SelectInterfaceForDestination(dst); idx.has_value()) {
    if (NetworkInterface* iface = GetInterface(*idx); iface != nullptr)
      link_mtu = iface->mtu;
  }
  return pmtu_cache.GetPathMtu(dst, link_mtu, std::chrono::steady_clock::now());
}

void UpdatePathMtu(const IpAddress& dst, uint16 new_pmtu) {
  uint16 link_mtu = 1500;
  if (auto idx = SelectInterfaceForDestination(dst); idx.has_value()) {
    if (NetworkInterface* iface = GetInterface(*idx); iface != nullptr)
      link_mtu = iface->mtu;
  }
  (void)pmtu_cache.OnPacketTooBig(dst, new_pmtu, link_mtu,
                                  std::chrono::steady_clock::now());
}

std::vector<IpAddress> GetActiveDnsServers() {
  std::vector<IpAddress> result;
  const auto now = std::chrono::steady_clock::now();
  for (size_t i = 0; i < interfaces.size(); ++i) {
    if (i < controllers.size() && controllers[i]) {
      for (const IpAddress& s : controllers[i]->settings.ipv4_dns_servers)
        result.push_back(s);
      for (const IpAddress& s : controllers[i]->settings.ipv6_dns_servers)
        result.push_back(s);
    }
    for (const DnsServer& srv : interfaces[i].dns_servers) {
      if (now >= srv.valid_until) continue;
      bool dup = false;
      for (const IpAddress& existing : result) {
        if (existing == srv.address) {
          dup = true;
          break;
        }
      }
      if (!dup) result.push_back(srv.address);
    }
  }
  if (result.empty()) result.push_back(kDefaultIpv4DnsServer);
  return result;
}

std::optional<std::pair<IpAddress, uint8>> GetActiveNat64Prefix() {
  for (const NetworkInterface& iface : interfaces) {
    if (iface.nat64_prefix.has_value() && iface.nat64_prefix->IsValid()) {
      return std::make_pair(iface.nat64_prefix->prefix,
                            iface.nat64_prefix->prefix_length);
    }
  }
  return std::nullopt;
}

void SetDiscoveredNat64Prefix(size_t iface_idx, const Nat64Prefix& prefix) {
  NetworkInterface* iface = GetInterface(iface_idx);
  if (iface == nullptr || !prefix.IsValid()) return;
  iface->nat64_prefix = prefix;
}

bool GetPreferIpv4Setting() { return global_settings.prefer_ipv4; }

bool GetHappyEyeballsEnabledSetting() {
  return global_settings.connect_strategy ==
         NetworkConnectStrategy::kHappyEyeballs;
}

void TickNetworkInterfaces() {
  const auto now = std::chrono::steady_clock::now();
  pmtu_cache.Purge(now);
  firewall.Purge(now);
  (void)reassembler.Purge(now);

  for (size_t idx = 0; idx < controllers.size(); ++idx) {
    auto& ctrl = controllers[idx];
    if (!ctrl) continue;
    if (ctrl->slaac) ctrl->slaac->OnTimer(now);
    if (ctrl->temp_addresses) ctrl->temp_addresses->OnTimer(now);
    if (ctrl->mld) ctrl->mld->OnTimer(now);
    if (ctrl->nud) ctrl->nud->OnTimer(now);
    if (ctrl->router_advertiser) ctrl->router_advertiser->OnTimer(now);
    if (ctrl->dhcpv6) ctrl->dhcpv6->OnTimer(now);
    if (ctrl->dhcpv4) ctrl->dhcpv4->OnTimer(now);
    SyncAndPublishInterface(idx, now);
  }
}

void ProcessIncomingNdpPacket(size_t iface_idx, const IpAddress& src_ip,
                              const IpAddress& dst_ip, uint8 hop_limit,
                              std::string_view icmpv6_packet) {
  NetworkInterface* iface = GetInterface(iface_idx);
  if (iface == nullptr || iface_idx >= controllers.size() ||
      !controllers[iface_idx] || icmpv6_packet.empty())
    return;
  auto& ctrl = controllers[iface_idx];
  const auto now = std::chrono::steady_clock::now();
  const uint8 icmp_type = static_cast<uint8>(icmpv6_packet[0]);

  Ipv6Header hdr;
  hdr.source = src_ip;
  hdr.destination = dst_ip;
  hdr.hop_limit = hop_limit;
  hdr.next_header = kProtocolIcmpv6;
  hdr.payload_length = static_cast<uint16>(icmpv6_packet.size());

  if (icmp_type == 133) {  // Router Solicitation
    auto rs = ParseRouterSolicitation(hdr, icmpv6_packet);
    if (rs.has_value() && ctrl->router_advertiser)
      ctrl->router_advertiser->OnRouterSolicitation(hdr, *rs, now);
  } else if (icmp_type == 134) {  // Router Advertisement
    auto ra = ParseRouterAdvertisement(hdr, icmpv6_packet);
    if (!ra.has_value()) return;
    if (ra->cur_hop_limit != 0) iface->ipv6_hop_limit = ra->cur_hop_limit;
    if (ra->mtu.has_value() && *ra->mtu >= kMinIpv6Mtu && *ra->mtu <= 65535)
      iface->mtu = static_cast<uint16>(*ra->mtu);
    if (ra->source_mac.has_value())
      RecordNeighborReachable(iface_idx, src_ip, *ra->source_mac);

    if (ra->router_lifetime_seconds > 0) {
      const auto expiry =
          now + std::chrono::seconds(ra->router_lifetime_seconds);
      bool updated = false;
      for (auto& r : iface->default_routers) {
        if (r.address == src_ip) {
          r.valid_until = expiry;
          updated = true;
          break;
        }
      }
      if (!updated) iface->default_routers.push_back({src_ip, expiry});
    } else {
      std::erase_if(iface->default_routers,
                    [&](const DefaultRouter& r) { return r.address == src_ip; });
    }

    for (const NdpPrefixInformation& pio : ra->prefixes) {
      if (pio.on_link && pio.valid_lifetime_seconds > 0) {
        InterfaceAddress on_link;
        on_link.address = MaskPrefix(pio.prefix, pio.prefix_length);
        on_link.prefix_length = pio.prefix_length;
        on_link.state = AddressState::Preferred;
        on_link.origin = AddressOrigin::Slaac;
        on_link.valid_until =
            (pio.valid_lifetime_seconds == 0xFFFFFFFFu)
                ? std::chrono::steady_clock::time_point::max()
                : (now + std::chrono::seconds(pio.valid_lifetime_seconds));
        bool found = false;
        for (auto& existing : iface->on_link_prefixes) {
          if (existing.address == on_link.address &&
              existing.prefix_length == on_link.prefix_length) {
            existing.valid_until = on_link.valid_until;
            found = true;
            break;
          }
        }
        if (!found) iface->on_link_prefixes.push_back(on_link);

        // On QEMU slirp's fec0::/64 network, seed fec0::3 as the default IPv6 DNS.
        if (on_link.address == kDefaultSlirpIpv6Prefix &&
            on_link.prefix_length == 64) {
          bool has_slirp_dns = false;
          for (const auto& d : iface->dns_servers) {
            if (d.address == kDefaultSlirpIpv6Dns) {
              has_slirp_dns = true;
              break;
            }
          }
          if (!has_slirp_dns)
            iface->dns_servers.push_back({kDefaultSlirpIpv6Dns});
        }
      }
      if (ctrl->temp_addresses)
        ctrl->temp_addresses->OnPrefixInformation(pio, now);
    }

    for (const NdpRdnssOption& rdnss : ra->rdnss) {
      for (const IpAddress& srv : rdnss.servers) {
        if (rdnss.lifetime_seconds == 0) {
          std::erase_if(iface->dns_servers,
                        [&](const DnsServer& d) { return d.address == srv; });
        } else {
          const auto expiry =
              (rdnss.lifetime_seconds == 0xFFFFFFFFu)
                  ? std::chrono::steady_clock::time_point::max()
                  : (now + std::chrono::seconds(rdnss.lifetime_seconds));
          bool found = false;
          for (auto& d : iface->dns_servers) {
            if (d.address == srv) {
              d.valid_until = expiry;
              found = true;
              break;
            }
          }
          if (!found) iface->dns_servers.push_back({srv, expiry});
        }
      }
    }

    for (const NdpPref64Option& p64 : ra->pref64) {
      if (p64.lifetime_seconds > 0) {
        iface->nat64_prefix =
            Nat64Prefix{p64.prefix, p64.prefix_length, p64.lifetime_seconds};
      }
    }

    if (ctrl->slaac) ctrl->slaac->OnRouterAdvertisement(*ra, now);
    if (ctrl->dhcpv6) {
      ctrl->dhcpv6->OnRouterAdvertisementFlags(ra->managed_flag,
                                               ra->other_config_flag, now);
    }
    SyncAndPublishInterface(iface_idx, now);
  } else if (icmp_type == 135) {  // Neighbor Solicitation
    auto ns = ParseNeighborSolicitation(hdr, icmpv6_packet);
    if (!ns.has_value()) return;
    if (!src_ip.IsUnspecified() && ns->source_mac.has_value())
      RecordUnsolicitedNeighbor(iface_idx, src_ip, *ns->source_mac, true);
    if (ctrl->slaac) ctrl->slaac->OnNeighborSolicitation(hdr, *ns, now);
    if (ctrl->temp_addresses)
      ctrl->temp_addresses->OnNeighborSolicitation(src_ip, *ns, now);
    SyncAndPublishInterface(iface_idx, now);

    const bool handled_by_slaac =
        ctrl->slaac != nullptr &&
        ctrl->slaac->FindAddress(ns->target) != nullptr;
    if (!handled_by_slaac && iface->HasAddress(ns->target)) {
      const bool solicited = !src_ip.IsUnspecified();
      const IpAddress reply_dst =
          solicited ? src_ip : AllNodesMulticastAddress();
      NdpNeighborAdvertisement na;
      na.router_flag =
          global_settings.ipv6_forwarding && ctrl->settings.ipv6_router_mode;
      na.solicited_flag = solicited;
      na.override_flag = true;
      na.target = ns->target;
      na.target_mac = iface->mac;
      std::string icmp_reply =
          BuildNeighborAdvertisement(ns->target, reply_dst, na);
      std::optional<std::array<uint8, 6>> dst_mac = ns->source_mac;
      if (!dst_mac.has_value()) dst_mac = MulticastMac(reply_dst);
      if (dst_mac.has_value()) {
        Ipv6Datagram dg;
        dg.source = ns->target;
        dg.destination = reply_dst;
        dg.next_header = kProtocolIcmpv6;
        dg.hop_limit = kNdpHopLimit;
        dg.payload = std::move(icmp_reply);
        SendEthernetFrame(iface_idx, *dst_mac, kEtherTypeIpv6,
                          SerializeIpv6Datagram(dg));
      }
    }
  } else if (icmp_type == 136) {  // Neighbor Advertisement
    auto na = ParseNeighborAdvertisement(hdr, icmpv6_packet);
    if (!na.has_value()) return;
    if (ctrl->slaac) ctrl->slaac->OnNeighborAdvertisement(*na, now);
    if (ctrl->temp_addresses)
      ctrl->temp_addresses->OnNeighborAdvertisement(*na, now);
    if (na->target_mac.has_value())
      RecordNeighborReachable(iface_idx, na->target, *na->target_mac);
    SyncAndPublishInterface(iface_idx, now);
  }
}

void ProcessIncomingMldQuery(size_t iface_idx, const IpAddress& src_ip,
                             uint8 hop_limit, bool has_router_alert,
                             std::string_view icmpv6_packet) {
  if (iface_idx >= controllers.size() || !controllers[iface_idx] ||
      !controllers[iface_idx]->mld)
    return;
  Ipv6Header hdr;
  hdr.source = src_ip;
  hdr.destination = AllNodesMulticastAddress();
  hdr.hop_limit = hop_limit;
  hdr.next_header = kProtocolIcmpv6;
  hdr.payload_length = static_cast<uint16>(icmpv6_packet.size());
  auto query = ParseMldQuery(hdr, has_router_alert, icmpv6_packet);
  if (query.has_value()) {
    controllers[iface_idx]->mld->OnQuery(*query,
                                         std::chrono::steady_clock::now());
  }
}

void ProcessIncomingDhcpv6Packet(size_t iface_idx,
                                 std::string_view udp_payload) {
  if (iface_idx >= controllers.size() || !controllers[iface_idx] ||
      !controllers[iface_idx]->dhcpv6)
    return;
  const auto now = std::chrono::steady_clock::now();
  controllers[iface_idx]->dhcpv6->OnPacket(udp_payload, now);
  SyncAndPublishInterface(iface_idx, now);
}

void ProcessIncomingDhcpv4Packet(size_t iface_idx,
                                 std::string_view udp_payload) {
  if (iface_idx >= controllers.size() || !controllers[iface_idx] ||
      !controllers[iface_idx]->dhcpv4)
    return;
  const auto now = std::chrono::steady_clock::now();
  controllers[iface_idx]->dhcpv4->OnPacket(udp_payload, now);
  SyncAndPublishInterface(iface_idx, now);
}

void ProcessIncomingArpForDhcpv4(size_t iface_idx, const IpAddress& sender_ip,
                                 const IpAddress& target_ip,
                                 const std::array<uint8, 6>& sender_mac) {
  if (iface_idx >= controllers.size() || !controllers[iface_idx] ||
      !controllers[iface_idx]->dhcpv4)
    return;
  const Dhcpv4State state = controllers[iface_idx]->dhcpv4->state();
  if (state != Dhcpv4State::LinkLocalProbing &&
      state != Dhcpv4State::LinkLocalBound)
    return;
  const auto now = std::chrono::steady_clock::now();
  controllers[iface_idx]->dhcpv4->OnArpPacket(sender_ip, target_ip, sender_mac,
                                              now);
  SyncAndPublishInterface(iface_idx, now);
}

RoutingTable& GetRoutingTable() { return routing_table; }
Firewall& GetFirewall() { return firewall; }
Reassembler& GetReassembler() { return reassembler; }
FragmentIdGenerator& GetFragmentIdGenerator() { return fragment_id_generator; }
Icmpv6RateLimiter& GetIcmpv6RateLimiter() { return icmpv6_rate_limiter; }
IpsecEngine& GetIpsecEngine() { return ipsec_engine; }

ForwardingConfig GetForwardingConfig() {
  return {global_settings.ipv4_forwarding, global_settings.ipv6_forwarding};
}
