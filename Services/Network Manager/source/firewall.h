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

#include <chrono>
#include <compare>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include "perception/network/ip_address.h"

// Direction of a packet relative to the host.
enum class FirewallDirection : uint8 { Inbound = 0, Outbound = 1, Any = 2 };

// Verdict returned by the firewall or configured on a rule.
enum class FirewallAction : uint8 { Allow = 0, Deny = 1 };

// An IPv4 or IPv6 CIDR prefix matcher. A default-constructed IpPrefix
// (Unspecified address, length 0) matches all addresses.
struct IpPrefix {
  // Network prefix address.
  ::perception::network::IpAddress prefix;
  // Number of leading bits to match.
  uint8 prefix_length = 0;

  // Returns true if `address` falls within this prefix.
  bool Matches(const ::perception::network::IpAddress& address) const;

  bool operator==(const IpPrefix& other) const = default;
};

// Inclusive transport port range [min_port, max_port].
struct PortRange {
  // Lowest matching port (host byte order).
  uint16 min_port = 0;
  // Highest matching port (host byte order).
  uint16 max_port = 65535;

  // Returns true if `port` is within [min_port, max_port].
  bool Matches(uint16 port) const {
    return port >= min_port && port <= max_port;
  }

  // Returns true if this range covers all ports [0, 65535].
  bool IsWildcard() const { return min_port == 0 && max_port == 65535; }

  bool operator==(const PortRange& other) const = default;
};

// An ordered packet-filter rule (decision Q22).
struct FirewallRule {
  // Verdict when the rule matches.
  FirewallAction action = FirewallAction::Deny;
  // Traffic direction to match (Any matches both Inbound and Outbound).
  FirewallDirection direction = FirewallDirection::Any;
  // Address family to match (Unspecified matches both V4 and V6).
  ::perception::network::IpAddressFamily family =
      ::perception::network::IpAddressFamily::Unspecified;
  // Upper-layer protocol to match (0 matches any protocol).
  uint8 protocol = 0;
  // Source CIDR prefix.
  IpPrefix src_prefix;
  // Destination CIDR prefix.
  IpPrefix dst_prefix;
  // Source port range (only constraining when non-wildcard on TCP/UDP).
  PortRange src_port_range;
  // Destination port range (only constraining when non-wildcard on TCP/UDP).
  PortRange dst_port_range;
  // Interface index to match (nullopt matches any interface).
  std::optional<uint32> interface_index;

  bool operator==(const FirewallRule& other) const = default;
};

// Parsed packet metadata inspected by the firewall.
struct FirewallPacket {
  // Packet direction (Inbound or Outbound).
  FirewallDirection direction = FirewallDirection::Inbound;
  // Source IP address.
  ::perception::network::IpAddress source;
  // Destination IP address.
  ::perception::network::IpAddress destination;
  // Upper-layer protocol (6 = TCP, 17 = UDP, 1 = ICMPv4, 58 = ICMPv6).
  uint8 protocol = 0;
  // Source port for TCP/UDP.
  uint16 src_port = 0;
  // Destination port for TCP/UDP.
  uint16 dst_port = 0;
  // ICMPv4 or ICMPv6 message type.
  uint8 icmp_type = 0;
  // ICMPv4 or ICMPv6 message code.
  uint8 icmp_code = 0;
  // Identifier field for ICMPv4/ICMPv6 Echo Request and Echo Reply.
  uint16 icmp_identifier = 0;
  // IPv4 TTL or IPv6 Hop Limit (used to verify hop limit 255 on NDP and 1 on
  // MLD per RFC 4890).
  uint8 hop_limit = 64;
  // Network interface index.
  uint32 interface_index = 0;

  // Builds a FirewallPacket by extracting TCP/UDP ports or ICMP type/code/echo
  // identifier from `upper_layer_payload`.
  static FirewallPacket FromPayload(
      FirewallDirection direction,
      const ::perception::network::IpAddress& source,
      const ::perception::network::IpAddress& destination, uint8 protocol,
      std::string_view upper_layer_payload, uint8 hop_limit = 64,
      uint32 interface_index = 0);
};

// Stateful packet-filter firewall (decision Q22):
// - Blocks all inbound traffic by default (even to listening sockets) unless
//   an ordered rule allows it or it matches an established/related flow.
// - Allows outbound traffic by default (unless an ordered rule denies it) and
//   records connection state for TCP, UDP, and ICMPv4/ICMPv6 Echo so return
//   traffic is permitted.
// - Always permits essential ICMPv6 traffic per RFC 4890 (NDP RS/RA/NS/NA/
//   Redirect with hop limit 255, MLD with hop limit 1, and Packet Too Big /
//   Destination Unreachable / Time Exceeded / Parameter Problem) so SLAAC,
//   NDP, and PMTUD function without manual rules.
class Firewall {
 public:
  // Appends `rule` to the end of the ordered rule list.
  void AddRule(const FirewallRule& rule);

  // Inserts `rule` at `index` (clamped to the current rule list size).
  void InsertRule(size_t index, const FirewallRule& rule);

  // Removes the rule at `index`. Returns false if `index` is out of bounds.
  bool RemoveRule(size_t index);

  // Removes all configured rules.
  void ClearRules();

  // Returns the ordered list of rules.
  const std::vector<FirewallRule>& rules() const { return rules_; }

  // Evaluates `packet` at `now`, updating state entries when a flow is
  // permitted.
  FirewallAction Evaluate(const FirewallPacket& packet,
                          std::chrono::steady_clock::time_point now);

  // Removes all expired state entries.
  void Purge(std::chrono::steady_clock::time_point now);

  // Returns the number of active state entries (expired or not).
  size_t StateCount() const { return states_.size(); }

 private:
  // Canonical key identifying a bidirectional flow in the state table.
  struct StateKey {
    // Address family.
    ::perception::network::IpAddressFamily family =
        ::perception::network::IpAddressFamily::Unspecified;
    // Upper-layer protocol (6 = TCP, 17 = UDP, 1 = ICMPv4, 58 = ICMPv6).
    uint8 protocol = 0;
    // Local host address.
    ::perception::network::IpAddress local_address;
    // Remote peer address.
    ::perception::network::IpAddress remote_address;
    // Local port (or ICMP Echo identifier).
    uint16 local_port = 0;
    // Remote port (or 0 for ICMP Echo).
    uint16 remote_port = 0;

    bool operator==(const StateKey& other) const = default;
    std::strong_ordering operator<=>(const StateKey& other) const = default;
  };

  // State entry tracking expiry of an active flow.
  struct StateEntry {
    // When this state entry expires if no further matching packets arrive.
    std::chrono::steady_clock::time_point expires;
  };

  // Builds the canonical StateKey for `packet` if `packet` is trackable in its
  // direction, or nullopt if the packet cannot match/create a state entry.
  static std::optional<StateKey> KeyForPacket(const FirewallPacket& packet);

  // Records or refreshes the state entry for an allowed packet.
  void RecordState(const FirewallPacket& packet,
                   std::chrono::steady_clock::time_point now);

  // Purges expired states and evicts the soonest-expiring state if at capacity.
  void MakeRoom(std::chrono::steady_clock::time_point now);

  // Ordered allow/deny rules.
  std::vector<FirewallRule> rules_;
  // Active flow states.
  std::map<StateKey, StateEntry> states_;
};
