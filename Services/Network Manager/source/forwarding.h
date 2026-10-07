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
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "perception/network/ip_address.h"
#include "routing_table.h"

// Per-family forwarding configuration knobs (both off by default).
struct ForwardingConfig {
  // Enables IPv4 packet forwarding between interfaces when true.
  bool ipv4_forwarding_enabled = false;
  // Enables IPv6 packet forwarding between interfaces when true.
  bool ipv6_forwarding_enabled = false;
};

// Lightweight interface descriptor used by the forwarding engine.
struct ForwardingInterface {
  // Interface index in the system table.
  size_t index = 0;
  // Link MTU in bytes.
  uint16 mtu = 1500;
  // Unicast addresses assigned to this interface.
  std::vector<::perception::network::IpAddress> addresses;
};

// Action returned by the forwarding engine for an incoming IP packet.
enum class ForwardingAction : uint8 {
  // Packet is addressed to a local interface and should be processed locally.
  DeliverLocally = 0,
  // Packet was decremented and routed to an outgoing interface.
  Forward = 1,
  // Packet could not be forwarded and an ICMPv4/ICMPv6 error was generated.
  SendIcmpError = 2,
  // Packet was silently dropped.
  Drop = 3,
};

// Reason a packet was dropped or rejected by the forwarding engine.
enum class ForwardingDropReason : uint8 {
  None = 0,
  MalformedPacket = 1,
  ForwardingDisabled = 2,
  LinkLocalOrMulticast = 3,
  NoRoute = 4,
  TimeExceeded = 5,
  PacketTooBig = 6,
};

// Complete result of evaluating an incoming IPv4 or IPv6 packet for forwarding.
struct ForwardingResult {
  // Action to take.
  ForwardingAction action = ForwardingAction::Drop;
  // Reason for dropping or generating an ICMP error.
  ForwardingDropReason reason = ForwardingDropReason::None;
  // Outgoing interface index (for Forward or SendIcmpError).
  size_t egress_interface_index = 0;
  // Immediate next-hop IP address on the outgoing interface.
  ::perception::network::IpAddress next_hop;
  // Forwarded IP packet (with decremented TTL/Hop Limit) or complete IP+ICMP
  // error packet ready for transmission.
  std::string packet;
  // ICMP/ICMPv6 type if action is SendIcmpError.
  uint8 icmp_type = 0;
  // ICMP/ICMPv6 code if action is SendIcmpError.
  uint8 icmp_code = 0;
  // Reported Next-Hop MTU if reason is PacketTooBig.
  uint16 reported_mtu = 0;
};

// Optional callback returning a cached Path MTU for a destination.
using PmtuLookup = std::function<std::optional<uint16>(
    const ::perception::network::IpAddress& destination)>;

// Evaluates an IPv4 or IPv6 packet received on `ingress_interface_index` for
// local delivery or forwarding across `interfaces` using `routing_table`.
ForwardingResult EvaluatePacketForwarding(
    std::string_view packet, size_t ingress_interface_index,
    const ForwardingConfig& config, const RoutingTable& routing_table,
    std::span<const ForwardingInterface> interfaces,
    const PmtuLookup& pmtu_lookup = {},
    std::optional<std::chrono::steady_clock::time_point> now = std::nullopt);
