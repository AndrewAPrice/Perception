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

#pragma once

#include <types.h>

#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dhcpv4.h"
#include "dhcpv6.h"
#include "ethernet.h"
#include "firewall.h"
#include "forwarding.h"
#include "fragmentation.h"
#include "icmpv6.h"
#include "interface_address.h"
#include "ipsec.h"
#include "mipv6.h"
#include "mld.h"
#include "nat64_clat.h"
#include "ndp.h"
#include "neighbor_cache.h"
#include "perception/devices/network_device.h"
#include "perception/network/ip_address.h"
#include "pmtu_cache.h"
#include "reassembly.h"
#include "routing_table.h"
#include "slaac.h"
#include "temporary_addresses.h"

// A default router (statically configured for IPv4, learned from RAs for IPv6).
struct DefaultRouter {
  // Router IP address (link-local for IPv6).
  ::perception::network::IpAddress address;
  // Router lifetime expiry (time_point::max() if static/infinite).
  std::chrono::steady_clock::time_point valid_until =
      std::chrono::steady_clock::time_point::max();
};

// A DNS recursive name server (static, RDNSS, or DHCPv6) with lifetime expiry.
struct DnsServer {
  // DNS server IP address.
  ::perception::network::IpAddress address;
  // Server lifetime expiry (time_point::max() if static/infinite).
  std::chrono::steady_clock::time_point valid_until =
      std::chrono::steady_clock::time_point::max();
};

// Represents an active network interface card (NIC).
struct NetworkInterface {
  // Client handle connected to the network driver instance.
  ::perception::devices::NetworkDevice::Client device;
  // Local MAC hardware address of this card.
  std::array<uint8, 6> mac{};
  // Assigned IPv4 and IPv6 addresses.
  std::vector<InterfaceAddress> addresses;
  // Configured or learned IPv4 and IPv6 default routers.
  std::vector<DefaultRouter> default_routers;
  // On-link prefixes (IPv4 subnet and IPv6 RA PIOs with L=1).
  std::vector<InterfaceAddress> on_link_prefixes;
  // Configured or learned DNS servers.
  std::vector<DnsServer> dns_servers;
  // Joined multicast groups with reference counts.
  std::map<::perception::network::IpAddress, int> multicast_groups;
  // Per-interface ARP and NDP neighbor cache.
  NeighborCache neighbor_cache;
  // Link MTU (RA MTU option or 1500).
  uint16 mtu = 1500;
  // Default hop limit for unicast IPv6 (RA CurHopLimit or 64).
  uint8 ipv6_hop_limit = 64;
  // Optional learned NAT64 prefix (RFC 8781 PREF64 or DNS64).
  std::optional<Nat64Prefix> nat64_prefix;

  // Returns a usable source address of `family` assigned to this interface,
  // preferring Preferred over Deprecated addresses.
  std::optional<::perception::network::IpAddress> GetPreferredAddress(
      ::perception::network::IpAddressFamily family) const;

  // Returns true if `address` is assigned to this interface and not Duplicate.
  bool HasAddress(const ::perception::network::IpAddress& address) const;

  // Returns true if `destination` is on-link for this interface (link-local,
  // matching an on-link prefix, or within an assigned IPv4 subnet).
  bool IsOnLink(const ::perception::network::IpAddress& destination) const;

  // Selects the immediate next-hop IP address to reach `destination` on this
  // interface, or nullopt if no route is available.
  std::optional<::perception::network::IpAddress> SelectNextHop(
      const ::perception::network::IpAddress& destination) const;

  // Returns true if an incoming Ethernet frame addressed to `dest_mac` should
  // be accepted by this interface.
  bool IsDestinedForMac(const std::array<uint8, 6>& dest_mac) const;
};

// Retrieves all registered active network interfaces.
const std::vector<NetworkInterface>& GetNetworkInterfaces();

// Retrieves a registered network interface by index.
NetworkInterface& GetNetworkInterface(size_t index);

// Retrieves a pointer to a registered network interface by index, or nullptr.
NetworkInterface* GetInterface(size_t index);

// Retrieves the total number of registered network interfaces.
size_t GetNetworkInterfaceCount();

// Adds a new network interface card with default configuration and returns its
// assigned index.
size_t AddNetworkInterface(NetworkInterface interface);

// Selects the outgoing interface index to reach `dst` via the routing table or
// interface default routers.
std::optional<size_t> SelectInterfaceForDestination(
    const ::perception::network::IpAddress& dst);

// Selects the best source address on `iface_idx` for `dst` per RFC 6724.
std::optional<::perception::network::IpAddress> SelectSourceAddress(
    size_t iface_idx, const ::perception::network::IpAddress& dst);

// Selects the immediate next-hop IP address on `iface_idx` to reach `dst`.
std::optional<::perception::network::IpAddress> SelectNextHop(
    size_t iface_idx, const ::perception::network::IpAddress& dst);

// Builds an Ethernet II frame, pads it to 60 bytes, and transmits it on
// interface `iface_idx`.
bool SendEthernetFrame(size_t iface_idx, const std::array<uint8, 6>& dest_mac,
                       uint16 ether_type, std::string_view payload);

// Resolves `next_hop` to a link-layer MAC address on interface `iface_idx`
// using the per-interface NeighborCache.
std::optional<std::array<uint8, 6>> ResolveNeighbor(
    size_t iface_idx, const ::perception::network::IpAddress& next_hop);

// Resolves `next_hop` on `iface_idx` and transmits `payload` with `ether_type`.
bool ResolveAndSendFrame(size_t iface_idx,
                         const ::perception::network::IpAddress& next_hop,
                         uint16 ether_type, std::string_view payload);

// Marks `ip` as Reachable with `mac` on `iface_idx`, waking any fibers blocked
// in ResolveNeighbor and transmitting any queued packets.
void RecordNeighborReachable(size_t iface_idx,
                             const ::perception::network::IpAddress& ip,
                             const std::array<uint8, 6>& mac);

// Records an unsolicited neighbor MAC (such as from an incoming ARP request),
// waking any fibers waiting on an Incomplete entry.
void RecordUnsolicitedNeighbor(size_t iface_idx,
                               const ::perception::network::IpAddress& ip,
                               const std::array<uint8, 6>& mac,
                               bool create_if_missing = true);

// Transmits an ARP request for `target_ip` on interface `iface_idx`.
void SendArpRequest(size_t iface_idx,
                    const ::perception::network::IpAddress& target_ip);

// Transmits a unicast ARP reply to `target_mac` / `target_ip` on `iface_idx`.
void SendArpReply(size_t iface_idx, const std::array<uint8, 6>& target_mac,
                  const ::perception::network::IpAddress& target_ip);

// Transmits an NDP Neighbor Solicitation for `target_ip` on `iface_idx`.
void SendNdpNeighborSolicitation(
    size_t iface_idx, const ::perception::network::IpAddress& target_ip,
    bool is_dad = false);

// Returns the effective Path MTU for `dst` across the outgoing interface and
// the shared PMTU cache.
uint16 GetEffectivePathMtu(const ::perception::network::IpAddress& dst);

// Records an ICMP Packet Too Big / Fragmentation Needed report for `dst`.
void UpdatePathMtu(const ::perception::network::IpAddress& dst,
                   uint16 new_pmtu);

// Returns the active recursive DNS servers across all interfaces.
std::vector<::perception::network::IpAddress> GetActiveDnsServers();

// Returns the active NAT64 prefix and prefix length if discovered via PREF64 or
// DNS64, or nullopt if none is active.
std::optional<std::pair<::perception::network::IpAddress, uint8>>
GetActiveNat64Prefix();

// Records a NAT64 prefix discovered via DNS64 on `iface_idx`.
void SetDiscoveredNat64Prefix(size_t iface_idx, const Nat64Prefix& prefix);

// Returns true if the global `preferIpv4` setting is enabled.
bool GetPreferIpv4Setting();

// Returns true if Happy Eyeballs v2 connection racing is enabled.
bool GetHappyEyeballsEnabledSetting();

// Advances all per-interface autoconfiguration, NDP/NUD, MLD, DHCPv4/v6,
// reassembly, PMTU, and firewall timers.
void TickNetworkInterfaces();

// Processes an incoming ICMPv6 NDP packet (RS, RA, NS, NA, Redirect) on
// `iface_idx`.
void ProcessIncomingNdpPacket(size_t iface_idx,
                              const ::perception::network::IpAddress& src_ip,
                              const ::perception::network::IpAddress& dst_ip,
                              uint8 hop_limit, std::string_view icmpv6_packet);

// Processes an incoming ICMPv6 MLD Query packet on `iface_idx`.
void ProcessIncomingMldQuery(size_t iface_idx,
                             const ::perception::network::IpAddress& src_ip,
                             uint8 hop_limit, bool has_router_alert,
                             std::string_view icmpv6_packet);

// Processes an incoming DHCPv6 UDP payload (port 546) on `iface_idx`.
void ProcessIncomingDhcpv6Packet(size_t iface_idx,
                                 std::string_view udp_payload);

// Processes an incoming DHCPv4 UDP payload (port 68) on `iface_idx`.
void ProcessIncomingDhcpv4Packet(size_t iface_idx,
                                 std::string_view udp_payload);

// Notifies the per-interface DHCPv4/RFC 3927 client of an incoming ARP packet.
void ProcessIncomingArpForDhcpv4(
    size_t iface_idx, const ::perception::network::IpAddress& sender_ip,
    const ::perception::network::IpAddress& target_ip,
    const std::array<uint8, 6>& sender_mac);

// Shared stack engines used by the IP TX and RX pipelines.
RoutingTable& GetRoutingTable();
Firewall& GetFirewall();
Reassembler& GetReassembler();
FragmentIdGenerator& GetFragmentIdGenerator();
Icmpv6RateLimiter& GetIcmpv6RateLimiter();
IpsecEngine& GetIpsecEngine();
ForwardingConfig GetForwardingConfig();
