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

#include "firewall.h"

#include <algorithm>

#include "wire_format.h"

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

namespace {

// Protocol number for ICMPv4.
constexpr uint8 kProtocolIcmpv4 = 1;

// Protocol number for TCP.
constexpr uint8 kProtocolTcp = 6;

// Protocol number for UDP.
constexpr uint8 kProtocolUdp = 17;

// Protocol number for ICMPv6.
constexpr uint8 kProtocolIcmpv6 = 58;

// ICMPv4 Echo Reply message type.
constexpr uint8 kIcmpv4EchoReply = 0;

// ICMPv4 Echo Request message type.
constexpr uint8 kIcmpv4EchoRequest = 8;

// ICMPv6 Destination Unreachable message type.
constexpr uint8 kIcmpv6DestinationUnreachable = 1;

// ICMPv6 Packet Too Big message type.
constexpr uint8 kIcmpv6PacketTooBig = 2;

// ICMPv6 Time Exceeded message type.
constexpr uint8 kIcmpv6TimeExceeded = 3;

// ICMPv6 Parameter Problem message type.
constexpr uint8 kIcmpv6ParameterProblem = 4;

// ICMPv6 Echo Request message type.
constexpr uint8 kIcmpv6EchoRequest = 128;

// ICMPv6 Echo Reply message type.
constexpr uint8 kIcmpv6EchoReply = 129;

// ICMPv6 MLD Multicast Listener Query message type.
constexpr uint8 kIcmpv6MldQuery = 130;

// ICMPv6 MLDv1 Multicast Listener Report message type.
constexpr uint8 kIcmpv6Mldv1Report = 131;

// ICMPv6 MLDv1 Multicast Listener Done message type.
constexpr uint8 kIcmpv6Mldv1Done = 132;

// ICMPv6 NDP Router Solicitation message type.
constexpr uint8 kIcmpv6RouterSolicitation = 133;

// ICMPv6 NDP Router Advertisement message type.
constexpr uint8 kIcmpv6RouterAdvertisement = 134;

// ICMPv6 NDP Neighbor Solicitation message type.
constexpr uint8 kIcmpv6NeighborSolicitation = 135;

// ICMPv6 NDP Neighbor Advertisement message type.
constexpr uint8 kIcmpv6NeighborAdvertisement = 136;

// ICMPv6 NDP Redirect message type.
constexpr uint8 kIcmpv6Redirect = 137;

// ICMPv6 Inverse Neighbor Discovery Solicitation message type.
constexpr uint8 kIcmpv6InverseNdSolicitation = 141;

// ICMPv6 Inverse Neighbor Discovery Advertisement message type.
constexpr uint8 kIcmpv6InverseNdAdvertisement = 142;

// ICMPv6 MLDv2 Multicast Listener Report message type.
constexpr uint8 kIcmpv6Mldv2Report = 143;

// Required IPv6 hop limit for Neighbor Discovery packets (RFC 4861).
constexpr uint8 kRequiredNdpHopLimit = 255;

// Required IPv6 hop limit for MLD packets (RFC 3810).
constexpr uint8 kRequiredMldHopLimit = 1;

// Lifetime of an active TCP state entry in seconds.
constexpr int kTcpStateLifetimeSeconds = 300;

// Lifetime of an active UDP state entry in seconds.
constexpr int kUdpStateLifetimeSeconds = 60;

// Lifetime of an active ICMP Echo state entry in seconds.
constexpr int kIcmpEchoStateLifetimeSeconds = 30;

// Maximum number of concurrent state entries tracked by the firewall.
constexpr size_t kMaxStates = 1024;

// Evaluates whether an ICMPv6 packet is an essential protocol message that
// must always be permitted (or dropped if its hop limit is invalid) per
// RFC 4890. Returns nullopt for non-essential ICMPv6 (such as Echo).
std::optional<FirewallAction> EvaluateEssentialIcmpv6(
    const FirewallPacket& packet) {
  if (!packet.source.IsV6() || !packet.destination.IsV6() ||
      packet.protocol != kProtocolIcmpv6)
    return std::nullopt;

  switch (packet.icmp_type) {
    case kIcmpv6DestinationUnreachable:
    case kIcmpv6PacketTooBig:
    case kIcmpv6TimeExceeded:
    case kIcmpv6ParameterProblem:
      return FirewallAction::Allow;

    case kIcmpv6RouterSolicitation:
    case kIcmpv6RouterAdvertisement:
    case kIcmpv6NeighborSolicitation:
    case kIcmpv6NeighborAdvertisement:
    case kIcmpv6Redirect:
    case kIcmpv6InverseNdSolicitation:
    case kIcmpv6InverseNdAdvertisement:
      return packet.hop_limit == kRequiredNdpHopLimit ? FirewallAction::Allow
                                                      : FirewallAction::Deny;

    case kIcmpv6MldQuery:
    case kIcmpv6Mldv1Report:
    case kIcmpv6Mldv1Done:
    case kIcmpv6Mldv2Report:
      if (packet.hop_limit != kRequiredMldHopLimit) return FirewallAction::Deny;
      if (!packet.source.IsLinkLocal() && !packet.source.IsUnspecified())
        return FirewallAction::Deny;
      return FirewallAction::Allow;

    default:
      return std::nullopt;
  }
}

// Returns true if `rule` matches `packet`.
bool RuleMatches(const FirewallRule& rule, const FirewallPacket& packet) {
  if (rule.direction != FirewallDirection::Any &&
      rule.direction != packet.direction)
    return false;
  if (rule.family != IpAddressFamily::Unspecified &&
      rule.family != packet.source.family())
    return false;
  if (rule.protocol != 0 && rule.protocol != packet.protocol) return false;
  if (!rule.src_prefix.Matches(packet.source)) return false;
  if (!rule.dst_prefix.Matches(packet.destination)) return false;

  bool has_port_constraint =
      !rule.src_port_range.IsWildcard() || !rule.dst_port_range.IsWildcard();
  if (has_port_constraint) {
    if (packet.protocol != kProtocolTcp && packet.protocol != kProtocolUdp)
      return false;
    if (!rule.src_port_range.Matches(packet.src_port) ||
        !rule.dst_port_range.Matches(packet.dst_port))
      return false;
  }

  if (rule.interface_index.has_value() &&
      *rule.interface_index != packet.interface_index)
    return false;
  return true;
}

// Returns the state lifetime duration for `protocol`.
std::chrono::seconds StateLifetimeForProtocol(uint8 protocol) {
  if (protocol == kProtocolTcp)
    return std::chrono::seconds(kTcpStateLifetimeSeconds);
  if (protocol == kProtocolUdp)
    return std::chrono::seconds(kUdpStateLifetimeSeconds);
  return std::chrono::seconds(kIcmpEchoStateLifetimeSeconds);
}

}  // namespace

bool IpPrefix::Matches(const IpAddress& address) const {
  if (prefix_length == 0 && prefix.IsUnspecified()) return true;
  return address.IsInPrefix(prefix, prefix_length);
}

FirewallPacket FirewallPacket::FromPayload(
    FirewallDirection direction, const IpAddress& source,
    const IpAddress& destination, uint8 protocol,
    std::string_view upper_layer_payload, uint8 hop_limit,
    uint32 interface_index) {
  FirewallPacket packet;
  packet.direction = direction;
  packet.source = source;
  packet.destination = destination;
  packet.protocol = protocol;
  packet.hop_limit = hop_limit;
  packet.interface_index = interface_index;

  WireReader reader(upper_layer_payload);
  if (protocol == kProtocolTcp || protocol == kProtocolUdp) {
    if (upper_layer_payload.size() >= 4) {
      packet.src_port = reader.ReadU16();
      packet.dst_port = reader.ReadU16();
    }
  } else if (protocol == kProtocolIcmpv4 || protocol == kProtocolIcmpv6) {
    if (upper_layer_payload.size() >= 2) {
      packet.icmp_type = reader.ReadU8();
      packet.icmp_code = reader.ReadU8();
      if (upper_layer_payload.size() >= 6) {
        reader.Skip(2);  // Checksum.
        packet.icmp_identifier = reader.ReadU16();
      }
    }
  }
  return packet;
}

void Firewall::AddRule(const FirewallRule& rule) { rules_.push_back(rule); }

void Firewall::InsertRule(size_t index, const FirewallRule& rule) {
  if (index >= rules_.size())
    rules_.push_back(rule);
  else
    rules_.insert(rules_.begin() + static_cast<ptrdiff_t>(index), rule);
}

bool Firewall::RemoveRule(size_t index) {
  if (index >= rules_.size()) return false;
  rules_.erase(rules_.begin() + static_cast<ptrdiff_t>(index));
  return true;
}

void Firewall::ClearRules() { rules_.clear(); }

std::optional<Firewall::StateKey> Firewall::KeyForPacket(
    const FirewallPacket& packet) {
  bool outbound = (packet.direction == FirewallDirection::Outbound);
  const IpAddress& local_addr = outbound ? packet.source : packet.destination;
  const IpAddress& remote_addr = outbound ? packet.destination : packet.source;

  if (packet.protocol == kProtocolTcp || packet.protocol == kProtocolUdp) {
    uint16 local_port = outbound ? packet.src_port : packet.dst_port;
    uint16 remote_port = outbound ? packet.dst_port : packet.src_port;
    return StateKey{packet.source.family(), packet.protocol, local_addr,
                    remote_addr,            local_port,      remote_port};
  }
  if (packet.protocol == kProtocolIcmpv4) {
    if (packet.icmp_type != kIcmpv4EchoRequest &&
        packet.icmp_type != kIcmpv4EchoReply)
      return std::nullopt;
    return StateKey{packet.source.family(), packet.protocol,
                    local_addr,             remote_addr,
                    packet.icmp_identifier, 0};
  }
  if (packet.protocol == kProtocolIcmpv6) {
    if (packet.icmp_type != kIcmpv6EchoRequest &&
        packet.icmp_type != kIcmpv6EchoReply)
      return std::nullopt;
    return StateKey{packet.source.family(), packet.protocol,
                    local_addr,             remote_addr,
                    packet.icmp_identifier, 0};
  }
  return std::nullopt;
}

FirewallAction Firewall::Evaluate(const FirewallPacket& packet,
                                  std::chrono::steady_clock::time_point now) {
  if (packet.source.family() != packet.destination.family() ||
      (packet.source.family() != IpAddressFamily::V4 &&
       packet.source.family() != IpAddressFamily::V6))
    return FirewallAction::Deny;

  // Essential ICMPv6 (NDP with hop limit 255, MLD with hop limit 1, and
  // connectivity/PMTUD error messages) is always permitted per RFC 4890, and
  // NDP/MLD with invalid hop limits is always dropped.
  if (auto essential = EvaluateEssentialIcmpv6(packet)) return *essential;

  // Check if the packet belongs to an established flow (or is an Echo Reply to
  // an active Echo Request). Unsolicited inbound Echo Requests do not match
  // existing Echo state entries.
  bool can_match_state = true;
  if (packet.direction == FirewallDirection::Inbound) {
    if (packet.protocol == kProtocolIcmpv4 &&
        packet.icmp_type != kIcmpv4EchoReply)
      can_match_state = false;
    if (packet.protocol == kProtocolIcmpv6 &&
        packet.icmp_type != kIcmpv6EchoReply)
      can_match_state = false;
  }

  if (can_match_state) {
    if (auto key = KeyForPacket(packet)) {
      auto it = states_.find(*key);
      if (it != states_.end()) {
        if (it->second.expires > now) {
          it->second.expires = now + StateLifetimeForProtocol(packet.protocol);
          return FirewallAction::Allow;
        }
        states_.erase(it);
      }
    }
  }

  // Evaluate ordered rules; first matching rule wins.
  for (const FirewallRule& rule : rules_) {
    if (!RuleMatches(rule, packet)) continue;
    if (rule.action == FirewallAction::Allow) RecordState(packet, now);
    return rule.action;
  }

  // Default policy: allow outbound (and track state), deny all inbound.
  if (packet.direction == FirewallDirection::Outbound) {
    RecordState(packet, now);
    return FirewallAction::Allow;
  }
  return FirewallAction::Deny;
}

void Firewall::RecordState(const FirewallPacket& packet,
                           std::chrono::steady_clock::time_point now) {
  auto key = KeyForPacket(packet);
  if (!key) return;
  if (!states_.contains(*key) && states_.size() >= kMaxStates) MakeRoom(now);
  states_[*key] = {now + StateLifetimeForProtocol(packet.protocol)};
}

void Firewall::Purge(std::chrono::steady_clock::time_point now) {
  std::erase_if(states_, [now](const auto& pair) {
    return pair.second.expires <= now;
  });
}

void Firewall::MakeRoom(std::chrono::steady_clock::time_point now) {
  Purge(now);
  if (states_.size() < kMaxStates) return;
  auto soonest = std::min_element(
      states_.begin(), states_.end(), [](const auto& a, const auto& b) {
        return a.second.expires < b.second.expires;
      });
  states_.erase(soonest);
}
