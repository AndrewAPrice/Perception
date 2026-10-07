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

#include "dhcpv4.h"

#include <algorithm>
#include <span>

#include "wire_format.h"

namespace {

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

// Size of the fixed BOOTP header before the DHCP magic cookie (236 bytes).
constexpr size_t kBootpHeaderSize = 236;

// Minimum DHCPv4 packet size (236-byte BOOTP header + 4-byte magic cookie).
constexpr size_t kMinDhcpv4PacketSize = kBootpHeaderSize + 4;

// BOOTP op code for client-to-server requests (BOOTREQUEST).
constexpr uint8 kBootpOpRequest = 1;

// BOOTP op code for server-to-client replies (BOOTREPLY).
constexpr uint8 kBootpOpReply = 2;

// ARP/BOOTP hardware type for 10Mb Ethernet (1).
constexpr uint8 kHardwareTypeEthernet = 1;

// Hardware address length for Ethernet (6 bytes).
constexpr uint8 kEthernetMacLength = 6;

// Size of the BOOTP chaddr field (16 bytes).
constexpr size_t kChaddrFieldSize = 16;

// Size of the BOOTP sname + file fields (64 + 128 = 192 bytes).
constexpr size_t kSnameAndFileSize = 192;

// Default lease time in seconds if a DHCPACK omits Option 51 (24 hours).
constexpr uint32 kDefaultDhcpv4LeaseSeconds = 86400;

// Default subnet mask prefix length if Option 1 is omitted (24).
constexpr uint8 kDefaultSubnetPrefixLength = 24;

// Prefix length for RFC 3927 IPv4 link-local addresses (169.254.0.0/16).
constexpr uint8 kLinkLocalPrefixLength = 16;

// Number of usable host addresses in 169.254.1.0..169.254.254.255
// (65536 - 512 = 65024, RFC 3927 §2.1).
constexpr uint32 kUsableLinkLocalAddressCount = 65024;

// Offset of the first usable link-local host (169.254.1.0 -> 256).
constexpr uint32 kFirstUsableLinkLocalOffset = 256;

// First octet of RFC 3927 IPv4 link-local addresses.
constexpr uint8 kLinkLocalOctet0 = 169;

// Second octet of RFC 3927 IPv4 link-local addresses.
constexpr uint8 kLinkLocalOctet1 = 254;

// Xorshift32 seed fallback if the MAC-derived seed is zero.
constexpr uint32 kDefaultPrngSeed = 0x6d2b79f5u;

// Reads a 4-byte IPv4 address from `reader`.
IpAddress ReadIpv4Address(WireReader& reader) {
  std::string_view raw = reader.ReadBytes(IpAddress::kV4Length);
  return IpAddress::FromBytes(
      IpAddressFamily::V4,
      std::span<const uint8>(reinterpret_cast<const uint8*>(raw.data()),
                             raw.size()));
}

// Writes a 4-byte IPv4 address to `writer`.
void WriteIpv4Address(WireWriter& writer, const IpAddress& addr) {
  if (addr.IsV4()) {
    for (size_t i = 0; i < IpAddress::kV4Length; ++i)
      writer.WriteU8(addr.bytes()[i]);
  } else {
    writer.WriteZeros(IpAddress::kV4Length);
  }
}

}  // namespace

std::optional<Dhcpv4Message> ParseDhcpv4Message(std::string_view udp_payload) {
  if (udp_payload.size() < kMinDhcpv4PacketSize) return std::nullopt;
  WireReader reader(udp_payload);

  const uint8 op = reader.ReadU8();
  const uint8 htype = reader.ReadU8();
  const uint8 hlen = reader.ReadU8();
  (void)reader.ReadU8();  // hops
  if (htype != kHardwareTypeEthernet || hlen != kEthernetMacLength)
    return std::nullopt;

  Dhcpv4Message msg;
  msg.op = op;
  msg.transaction_id = reader.ReadU32();
  msg.seconds_elapsed = reader.ReadU16();
  msg.flags = reader.ReadU16();
  msg.client_ip = ReadIpv4Address(reader);
  msg.your_ip = ReadIpv4Address(reader);
  msg.next_server_ip = ReadIpv4Address(reader);
  (void)ReadIpv4Address(reader);  // giaddr

  std::string_view chaddr = reader.ReadBytes(kChaddrFieldSize);
  if (!reader.ok()) return std::nullopt;
  for (size_t i = 0; i < kEthernetMacLength; ++i)
    msg.client_mac[i] = static_cast<uint8>(chaddr[i]);

  reader.Skip(kSnameAndFileSize);
  const uint32 cookie = reader.ReadU32();
  if (!reader.ok() || cookie != kDhcpv4MagicCookie) return std::nullopt;

  bool has_message_type = false;
  while (reader.Remaining() > 0) {
    const uint8 code = reader.ReadU8();
    if (code == static_cast<uint8>(Dhcpv4OptionCode::Pad)) continue;
    if (code == static_cast<uint8>(Dhcpv4OptionCode::End)) break;
    if (reader.Remaining() < 1) return std::nullopt;
    const uint8 opt_len = reader.ReadU8();
    std::string_view opt_data = reader.ReadBytes(opt_len);
    if (!reader.ok() || opt_data.size() != opt_len) return std::nullopt;

    WireReader opt_reader(opt_data);
    switch (static_cast<Dhcpv4OptionCode>(code)) {
      case Dhcpv4OptionCode::SubnetMask:
        if (opt_len == IpAddress::kV4Length)
          msg.subnet_mask = ReadIpv4Address(opt_reader);
        break;
      case Dhcpv4OptionCode::Router:
        if (opt_len >= IpAddress::kV4Length &&
            (opt_len % IpAddress::kV4Length) == 0) {
          while (opt_reader.Remaining() >= IpAddress::kV4Length)
            msg.routers.push_back(ReadIpv4Address(opt_reader));
        }
        break;
      case Dhcpv4OptionCode::DnsServers:
        if (opt_len >= IpAddress::kV4Length &&
            (opt_len % IpAddress::kV4Length) == 0) {
          while (opt_reader.Remaining() >= IpAddress::kV4Length)
            msg.dns_servers.push_back(ReadIpv4Address(opt_reader));
        }
        break;
      case Dhcpv4OptionCode::RequestedIpAddress:
        if (opt_len == IpAddress::kV4Length)
          msg.requested_ip = ReadIpv4Address(opt_reader);
        break;
      case Dhcpv4OptionCode::IpAddressLeaseTime:
        if (opt_len == 4) msg.lease_time_seconds = opt_reader.ReadU32();
        break;
      case Dhcpv4OptionCode::MessageType:
        if (opt_len == 1) {
          const uint8 raw_type = opt_reader.ReadU8();
          if (raw_type >= static_cast<uint8>(Dhcpv4MessageType::Discover) &&
              raw_type <= static_cast<uint8>(Dhcpv4MessageType::Inform)) {
            msg.type = static_cast<Dhcpv4MessageType>(raw_type);
            has_message_type = true;
          }
        }
        break;
      case Dhcpv4OptionCode::ServerIdentifier:
        if (opt_len == IpAddress::kV4Length)
          msg.server_id = ReadIpv4Address(opt_reader);
        break;
      case Dhcpv4OptionCode::ParameterRequestList:
        for (char c : opt_data)
          msg.parameter_request_list.push_back(static_cast<uint8>(c));
        break;
      case Dhcpv4OptionCode::RenewalTimeValue:
        if (opt_len == 4) msg.t1_seconds = opt_reader.ReadU32();
        break;
      case Dhcpv4OptionCode::RebindingTimeValue:
        if (opt_len == 4) msg.t2_seconds = opt_reader.ReadU32();
        break;
      default:
        break;
    }
  }

  if (!has_message_type) return std::nullopt;
  return msg;
}

std::string BuildDhcpv4Message(const Dhcpv4Message& message) {
  std::string out;
  out.reserve(300);
  WireWriter writer(out);
  writer.WriteU8(message.op);
  writer.WriteU8(kHardwareTypeEthernet);
  writer.WriteU8(kEthernetMacLength);
  writer.WriteU8(0);  // hops
  writer.WriteU32(message.transaction_id);
  writer.WriteU16(message.seconds_elapsed);
  writer.WriteU16(message.flags);
  WriteIpv4Address(writer, message.client_ip);
  WriteIpv4Address(writer, message.your_ip);
  WriteIpv4Address(writer, message.next_server_ip);
  writer.WriteZeros(IpAddress::kV4Length);  // giaddr
  for (uint8 b : message.client_mac) writer.WriteU8(b);
  writer.WriteZeros(kChaddrFieldSize - message.client_mac.size());
  writer.WriteZeros(kSnameAndFileSize);
  writer.WriteU32(kDhcpv4MagicCookie);

  // Option 53: DHCP Message Type.
  writer.WriteU8(static_cast<uint8>(Dhcpv4OptionCode::MessageType));
  writer.WriteU8(1);
  writer.WriteU8(static_cast<uint8>(message.type));

  // Option 61: Client Identifier (htype 1 + 6-byte MAC).
  writer.WriteU8(static_cast<uint8>(Dhcpv4OptionCode::ClientIdentifier));
  writer.WriteU8(1 + kEthernetMacLength);
  writer.WriteU8(kHardwareTypeEthernet);
  for (uint8 b : message.client_mac) writer.WriteU8(b);

  if (message.requested_ip.has_value()) {
    writer.WriteU8(static_cast<uint8>(Dhcpv4OptionCode::RequestedIpAddress));
    writer.WriteU8(IpAddress::kV4Length);
    WriteIpv4Address(writer, *message.requested_ip);
  }

  if (message.server_id.has_value()) {
    writer.WriteU8(static_cast<uint8>(Dhcpv4OptionCode::ServerIdentifier));
    writer.WriteU8(IpAddress::kV4Length);
    WriteIpv4Address(writer, *message.server_id);
  }

  if (message.subnet_mask.has_value()) {
    writer.WriteU8(static_cast<uint8>(Dhcpv4OptionCode::SubnetMask));
    writer.WriteU8(IpAddress::kV4Length);
    WriteIpv4Address(writer, *message.subnet_mask);
  }

  if (!message.routers.empty()) {
    writer.WriteU8(static_cast<uint8>(Dhcpv4OptionCode::Router));
    writer.WriteU8(
        static_cast<uint8>(message.routers.size() * IpAddress::kV4Length));
    for (const auto& router : message.routers) WriteIpv4Address(writer, router);
  }

  if (!message.dns_servers.empty()) {
    writer.WriteU8(static_cast<uint8>(Dhcpv4OptionCode::DnsServers));
    writer.WriteU8(
        static_cast<uint8>(message.dns_servers.size() * IpAddress::kV4Length));
    for (const auto& dns : message.dns_servers) WriteIpv4Address(writer, dns);
  }

  if (message.lease_time_seconds.has_value()) {
    writer.WriteU8(static_cast<uint8>(Dhcpv4OptionCode::IpAddressLeaseTime));
    writer.WriteU8(4);
    writer.WriteU32(*message.lease_time_seconds);
  }

  if (message.t1_seconds.has_value()) {
    writer.WriteU8(static_cast<uint8>(Dhcpv4OptionCode::RenewalTimeValue));
    writer.WriteU8(4);
    writer.WriteU32(*message.t1_seconds);
  }

  if (message.t2_seconds.has_value()) {
    writer.WriteU8(static_cast<uint8>(Dhcpv4OptionCode::RebindingTimeValue));
    writer.WriteU8(4);
    writer.WriteU32(*message.t2_seconds);
  }

  if (!message.parameter_request_list.empty()) {
    writer.WriteU8(static_cast<uint8>(Dhcpv4OptionCode::ParameterRequestList));
    writer.WriteU8(static_cast<uint8>(message.parameter_request_list.size()));
    for (uint8 code : message.parameter_request_list) writer.WriteU8(code);
  }

  writer.WriteU8(static_cast<uint8>(Dhcpv4OptionCode::End));
  return out;
}

uint8 SubnetMaskToPrefixLength(const IpAddress& subnet_mask) {
  if (!subnet_mask.IsV4()) return kDefaultSubnetPrefixLength;
  uint8 bits = 0;
  for (size_t i = 0; i < IpAddress::kV4Length; ++i) {
    uint8 b = subnet_mask.bytes()[i];
    for (int bit = 7; bit >= 0; --bit) {
      if ((b & (1u << bit)) != 0) {
        ++bits;
      } else {
        return bits;
      }
    }
  }
  return bits;
}

IpAddress SelectIpv4LinkLocalCandidate(uint32 random_value) {
  const uint32 host =
      kFirstUsableLinkLocalOffset + (random_value % kUsableLinkLocalAddressCount);
  const uint8 octet2 = static_cast<uint8>((host >> 8) & 0xffu);
  const uint8 octet3 = static_cast<uint8>(host & 0xffu);
  return IpAddress::V4(kLinkLocalOctet0, kLinkLocalOctet1, octet2, octet3);
}

Dhcpv4Client::Dhcpv4Client(const HardwareAddress& mac, Dhcpv4SendFn send_fn,
                           ArpProbeFn arp_probe_fn, Random32Fn random_fn)
    : mac_(mac),
      send_fn_(std::move(send_fn)),
      arp_probe_fn_(std::move(arp_probe_fn)),
      random_fn_(std::move(random_fn)) {
  for (uint8 b : mac) prng_state_ = (prng_state_ * 131u) ^ b;
  if (prng_state_ == 0) prng_state_ = kDefaultPrngSeed;
}

void Dhcpv4Client::Start(std::chrono::steady_clock::time_point now) {
  state_ = Dhcpv4State::Selecting;
  active_xid_ = NextRandom32();
  exchange_start_ = now;
  current_rto_ = kInitialRetransmitTimeout;
  attempts_ = 0;
  link_local_deadline_.reset();
  SendDiscover(now);
}

void Dhcpv4Client::Release(std::chrono::steady_clock::time_point now) {
  if ((state_ == Dhcpv4State::Bound || state_ == Dhcpv4State::Renewing ||
       state_ == Dhcpv4State::Rebinding) &&
      address_.has_value() && server_id_.has_value() && send_fn_) {
    Dhcpv4Message rel;
    rel.op = kBootpOpRequest;
    rel.transaction_id = NextRandom32();
    rel.client_ip = address_->address;
    rel.client_mac = mac_;
    rel.type = Dhcpv4MessageType::Release;
    rel.server_id = server_id_;
    send_fn_(BuildDhcpv4Message(rel), address_->address, *server_id_);
  }
  state_ = Dhcpv4State::Idle;
  address_.reset();
  subnet_mask_.reset();
  default_router_.reset();
  dns_servers_.clear();
  server_id_.reset();
  retransmit_deadline_.reset();
  t1_deadline_.reset();
  t2_deadline_.reset();
  link_local_deadline_.reset();
  (void)now;
}

void Dhcpv4Client::OnPacket(std::string_view udp_payload,
                            std::chrono::steady_clock::time_point now) {
  auto parsed = ParseDhcpv4Message(udp_payload);
  if (!parsed.has_value()) return;
  if (parsed->op != kBootpOpReply) return;
  if (parsed->client_mac != mac_) return;
  if (parsed->transaction_id != active_xid_) return;

  if (parsed->type == Dhcpv4MessageType::Offer) {
    if (state_ != Dhcpv4State::Selecting &&
        state_ != Dhcpv4State::LinkLocalProbing &&
        state_ != Dhcpv4State::LinkLocalBound)
      return;
    if (parsed->your_ip.IsUnspecified() || !parsed->server_id.has_value())
      return;

    offered_ip_ = parsed->your_ip;
    server_id_ = parsed->server_id;
    state_ = Dhcpv4State::Requesting;
    current_rto_ = kInitialRetransmitTimeout;
    attempts_ = 0;
    link_local_deadline_.reset();
    SendRequest(now);
    return;
  }

  if (parsed->type == Dhcpv4MessageType::Ack) {
    if (state_ != Dhcpv4State::Requesting && state_ != Dhcpv4State::Renewing &&
        state_ != Dhcpv4State::Rebinding)
      return;
    if (parsed->your_ip.IsUnspecified()) return;
    ApplyAck(*parsed, now);
    return;
  }

  if (parsed->type == Dhcpv4MessageType::Nak) {
    if (state_ == Dhcpv4State::Requesting || state_ == Dhcpv4State::Renewing ||
        state_ == Dhcpv4State::Rebinding) {
      address_.reset();
      subnet_mask_.reset();
      default_router_.reset();
      dns_servers_.clear();
      t1_deadline_.reset();
      t2_deadline_.reset();
      Start(now);
    }
  }
}

void Dhcpv4Client::OnArpPacket(const IpAddress& sender_ip,
                               const IpAddress& target_ip,
                               const HardwareAddress& sender_mac,
                               std::chrono::steady_clock::time_point now) {
  if (sender_mac == mac_) return;

  if (state_ == Dhcpv4State::LinkLocalProbing) {
    const bool sender_conflict = (sender_ip == link_local_candidate_);
    const bool probe_conflict =
        sender_ip.IsUnspecified() && (target_ip == link_local_candidate_);
    if (sender_conflict || probe_conflict) StartLinkLocalProbing(now);
    return;
  }

  if (state_ == Dhcpv4State::LinkLocalBound && address_.has_value() &&
      sender_ip == address_->address) {
    address_.reset();
    subnet_mask_.reset();
    state_ = Dhcpv4State::LinkLocalProbing;
    StartLinkLocalProbing(now);
  }
}

void Dhcpv4Client::OnTimer(std::chrono::steady_clock::time_point now) {
  if (state_ == Dhcpv4State::Idle) return;

  // Check lease expiration in Bound/Renewing/Rebinding.
  if ((state_ == Dhcpv4State::Bound || state_ == Dhcpv4State::Renewing ||
       state_ == Dhcpv4State::Rebinding) &&
      address_.has_value() && now >= address_->valid_until) {
    address_.reset();
    subnet_mask_.reset();
    default_router_.reset();
    dns_servers_.clear();
    t1_deadline_.reset();
    t2_deadline_.reset();
    Start(now);
    return;
  }

  // Check T2 transition (Renewing -> Rebinding).
  if ((state_ == Dhcpv4State::Bound || state_ == Dhcpv4State::Renewing) &&
      t2_deadline_.has_value() && now >= *t2_deadline_) {
    state_ = Dhcpv4State::Rebinding;
    active_xid_ = NextRandom32();
    current_rto_ = kInitialRetransmitTimeout;
    attempts_ = 0;
    SendRequest(now);
    return;
  }

  // Check T1 transition (Bound -> Renewing).
  if (state_ == Dhcpv4State::Bound && t1_deadline_.has_value() &&
      now >= *t1_deadline_) {
    state_ = Dhcpv4State::Renewing;
    active_xid_ = NextRandom32();
    current_rto_ = kInitialRetransmitTimeout;
    attempts_ = 0;
    SendRequest(now);
    return;
  }

  // Advance RFC 3927 link-local ARP probing.
  if (state_ == Dhcpv4State::LinkLocalProbing &&
      link_local_deadline_.has_value() && now >= *link_local_deadline_) {
    if (link_local_probes_sent_ < kLinkLocalProbeCount) {
      ++link_local_probes_sent_;
      link_local_deadline_ = now + kLinkLocalProbeInterval;
      if (arp_probe_fn_)
        arp_probe_fn_(IpAddress::V4Any(), link_local_candidate_);
    } else {
      // All ARP probes succeeded without conflict; announce and bind 169.254.x.y/16.
      if (arp_probe_fn_)
        arp_probe_fn_(link_local_candidate_, link_local_candidate_);
      InterfaceAddress ll;
      ll.address = link_local_candidate_;
      ll.prefix_length = kLinkLocalPrefixLength;
      ll.state = AddressState::Preferred;
      ll.origin = AddressOrigin::LinkLocal;
      ll.preferred_until = std::chrono::steady_clock::time_point::max();
      ll.valid_until = std::chrono::steady_clock::time_point::max();
      address_ = ll;
      subnet_mask_ = IpAddress::V4(255, 255, 0, 0);
      state_ = Dhcpv4State::LinkLocalBound;
      link_local_deadline_.reset();
    }
  }

  // Advance DHCPv4 retransmission / background discovery timer.
  if (retransmit_deadline_.has_value() && now >= *retransmit_deadline_) {
    if (state_ == Dhcpv4State::Selecting) {
      if (attempts_ >= kDiscoverAttemptsBeforeLinkLocal) {
        state_ = Dhcpv4State::LinkLocalProbing;
        StartLinkLocalProbing(now);
        retransmit_deadline_ = now + kBackgroundDiscoverInterval;
        return;
      }
      SendDiscover(now);
      return;
    }

    if (state_ == Dhcpv4State::LinkLocalProbing ||
        state_ == Dhcpv4State::LinkLocalBound) {
      active_xid_ = NextRandom32();
      SendDiscover(now);
      retransmit_deadline_ = now + kBackgroundDiscoverInterval;
      return;
    }

    if (state_ == Dhcpv4State::Requesting || state_ == Dhcpv4State::Renewing ||
        state_ == Dhcpv4State::Rebinding) {
      if (state_ == Dhcpv4State::Requesting &&
          attempts_ >= kDiscoverAttemptsBeforeLinkLocal) {
        Start(now);
        return;
      }
      SendRequest(now);
    }
  }
}

std::optional<std::chrono::steady_clock::time_point>
Dhcpv4Client::NextDeadline() const {
  std::optional<std::chrono::steady_clock::time_point> earliest;
  auto consider = [&](std::optional<std::chrono::steady_clock::time_point> t) {
    if (!t.has_value()) return;
    if (!earliest.has_value() || *t < *earliest) earliest = *t;
  };
  consider(retransmit_deadline_);
  consider(t1_deadline_);
  consider(t2_deadline_);
  consider(link_local_deadline_);
  if (address_.has_value() &&
      address_->valid_until != std::chrono::steady_clock::time_point::max())
    consider(address_->valid_until);
  return earliest;
}

uint32 Dhcpv4Client::NextRandom32() {
  if (random_fn_) return random_fn_();
  uint32 x = prng_state_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  prng_state_ = x;
  return x;
}

void Dhcpv4Client::SendDiscover(std::chrono::steady_clock::time_point now) {
  Dhcpv4Message msg;
  msg.op = kBootpOpRequest;
  msg.transaction_id = active_xid_;
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::seconds>(now - exchange_start_)
          .count();
  msg.seconds_elapsed =
      static_cast<uint16>(std::clamp<int64>(elapsed, 0, 0xffff));
  msg.flags = kDhcpv4BroadcastFlag;
  msg.client_mac = mac_;
  msg.type = Dhcpv4MessageType::Discover;
  msg.parameter_request_list = {
      static_cast<uint8>(Dhcpv4OptionCode::SubnetMask),
      static_cast<uint8>(Dhcpv4OptionCode::Router),
      static_cast<uint8>(Dhcpv4OptionCode::DnsServers),
  };

  ++attempts_;
  retransmit_deadline_ = now + current_rto_;
  current_rto_ = std::min(current_rto_ * 2, kMaxRetransmitTimeout);
  if (send_fn_)
    send_fn_(BuildDhcpv4Message(msg), IpAddress::V4Any(),
             IpAddress::V4Broadcast());
}

void Dhcpv4Client::SendRequest(std::chrono::steady_clock::time_point now) {
  Dhcpv4Message msg;
  msg.op = kBootpOpRequest;
  msg.transaction_id = active_xid_;
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::seconds>(now - exchange_start_)
          .count();
  msg.seconds_elapsed =
      static_cast<uint16>(std::clamp<int64>(elapsed, 0, 0xffff));
  msg.client_mac = mac_;
  msg.type = Dhcpv4MessageType::Request;
  msg.parameter_request_list = {
      static_cast<uint8>(Dhcpv4OptionCode::SubnetMask),
      static_cast<uint8>(Dhcpv4OptionCode::Router),
      static_cast<uint8>(Dhcpv4OptionCode::DnsServers),
  };

  IpAddress src_ip = IpAddress::V4Any();
  IpAddress dst_ip = IpAddress::V4Broadcast();
  if (state_ == Dhcpv4State::Requesting) {
    msg.flags = kDhcpv4BroadcastFlag;
    msg.requested_ip = offered_ip_;
    msg.server_id = server_id_;
  } else if (state_ == Dhcpv4State::Renewing && address_.has_value()) {
    msg.client_ip = address_->address;
    src_ip = address_->address;
    if (server_id_.has_value()) dst_ip = *server_id_;
  } else if (state_ == Dhcpv4State::Rebinding && address_.has_value()) {
    msg.flags = kDhcpv4BroadcastFlag;
    msg.client_ip = address_->address;
    src_ip = address_->address;
  }

  ++attempts_;
  retransmit_deadline_ = now + current_rto_;
  current_rto_ = std::min(current_rto_ * 2, kMaxRetransmitTimeout);
  if (send_fn_) send_fn_(BuildDhcpv4Message(msg), src_ip, dst_ip);
}

void Dhcpv4Client::StartLinkLocalProbing(
    std::chrono::steady_clock::time_point now) {
  link_local_candidate_ = SelectIpv4LinkLocalCandidate(NextRandom32());
  link_local_probes_sent_ = 1;
  link_local_deadline_ = now + kLinkLocalProbeInterval;
  if (arp_probe_fn_) arp_probe_fn_(IpAddress::V4Any(), link_local_candidate_);
}

void Dhcpv4Client::ApplyAck(const Dhcpv4Message& ack,
                            std::chrono::steady_clock::time_point now) {
  const uint32 lease_sec =
      ack.lease_time_seconds.value_or(kDefaultDhcpv4LeaseSeconds);
  const uint32 t1_sec = ack.t1_seconds.value_or(lease_sec / 2);
  const uint32 t2_sec =
      ack.t2_seconds.value_or(lease_sec - (lease_sec / 8));

  subnet_mask_ = ack.subnet_mask.value_or(IpAddress::V4(255, 255, 255, 0));
  if (!ack.routers.empty()) default_router_ = ack.routers.front();
  if (!ack.dns_servers.empty()) dns_servers_ = ack.dns_servers;
  if (ack.server_id.has_value()) server_id_ = ack.server_id;

  InterfaceAddress leased;
  leased.address = ack.your_ip;
  leased.prefix_length = SubnetMaskToPrefixLength(*subnet_mask_);
  leased.state = AddressState::Preferred;
  leased.origin = AddressOrigin::Dhcpv4;
  leased.preferred_until = now + std::chrono::seconds(lease_sec);
  leased.valid_until = now + std::chrono::seconds(lease_sec);
  address_ = leased;

  state_ = Dhcpv4State::Bound;
  retransmit_deadline_.reset();
  link_local_deadline_.reset();
  t1_deadline_ = now + std::chrono::seconds(t1_sec);
  t2_deadline_ = now + std::chrono::seconds(t2_sec);
}
