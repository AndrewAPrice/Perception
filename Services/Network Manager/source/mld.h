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
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ipv6_header.h"
#include "perception/network/ip_address.h"

// MLDv2 Multicast Address Record types (RFC 3810 §5.2.12).
enum class MldRecordType : uint8 {
  ModeIsInclude = 1,
  ModeIsExclude = 2,
  ChangeToIncludeMode = 3,
  ChangeToExcludeMode = 4,
  AllowNewSources = 5,
  BlockOldSources = 6,
};

// A single Multicast Address Record inside an MLDv2 Report.
struct MldAddressRecord {
  // Record type.
  MldRecordType record_type = MldRecordType::ModeIsExclude;
  // Multicast group address.
  ::perception::network::IpAddress multicast_address;
  // Unicast source addresses (empty for ASM any-source multicast).
  std::vector<::perception::network::IpAddress> sources;
};

// Parsed MLDv1 or MLDv2 Multicast Listener Query (ICMPv6 type 130).
struct MldQuery {
  // True if the query is a 24-byte MLDv1 Query (RFC 2710).
  bool is_v1 = false;
  // Decoded Maximum Response Delay in milliseconds.
  std::chrono::milliseconds max_response_delay{0};
  // Queried multicast address (:: for a General Query).
  ::perception::network::IpAddress multicast_address;
  // Suppress Router-Side Processing flag (MLDv2 only).
  bool suppress_router_processing = false;
  // Querier's Robustness Variable (MLDv2 only).
  uint8 qrv = 2;
  // Decoded Querier's Query Interval in seconds (MLDv2 only).
  uint32 qqi_seconds = 125;
  // Queried source addresses (MLDv2 Group-and-Source-Specific Query).
  std::vector<::perception::network::IpAddress> sources;
};

// Decodes an MLDv2 16-bit Maximum Response Code into milliseconds (RFC 3810
// §5.1.3).
std::chrono::milliseconds DecodeMldv2MaxResponseCode(uint16 code);

// Parses and validates an MLDv1 or MLDv2 Query. Requires `hop_limit == 1`,
// `has_router_alert == true`, and a link-local source address.
std::optional<MldQuery> ParseMldQuery(const Ipv6Header& ipv6_header,
                                      bool has_router_alert,
                                      std::string_view icmpv6_packet);

// Builds an MLDv2 Version 2 Multicast Listener Report (ICMPv6 type 143) with
// checksum.
std::string BuildMldv2Report(
    const ::perception::network::IpAddress& source,
    const ::perception::network::IpAddress& destination,
    const std::vector<MldAddressRecord>& records);

// Parses an MLDv2 Report payload into its Multicast Address Records.
std::optional<std::vector<MldAddressRecord>> ParseMldv2Report(
    std::string_view icmpv6_packet);

// Builds an MLDv1 Report (type 131) or MLDv1 Done (type 132) message with
// checksum.
std::string BuildMldv1Message(
    const ::perception::network::IpAddress& source,
    const ::perception::network::IpAddress& destination, bool is_done,
    const ::perception::network::IpAddress& multicast_address);

// Returns true if `group` is a multicast group that MLD reports (multicast
// with scope >= link-local and not the all-nodes address ff02::1).
bool IsMldReportableGroup(const ::perception::network::IpAddress& group);

// Source of random delays in milliseconds in the closed interval [0, max_ms].
using MldRandomDelayFn = std::function<uint32(uint32 max_ms)>;

// Pure RFC 3810 MLDv2 (with MLDv1 compatibility) multicast group membership
// manager for a single network interface.
class MldManager {
 public:
  // Default Unsolicited Report Interval for state-change retransmissions (1 s).
  static constexpr auto kUnsolicitedReportInterval =
      std::chrono::milliseconds(1000);

  // Default Older Version Querier Present Timeout (260 seconds, RFC 3810 §9.12).
  static constexpr auto kMldv1CompatibilityTimeout =
      std::chrono::milliseconds(260000);

  explicit MldManager(Ipv6Sink send_sink, MldRandomDelayFn random_delay = {});

  // Sets the link-local source address used for MLD transmissions (may be ::
  // while link-local DAD is still tentative).
  void SetLinkLocalSource(const ::perception::network::IpAddress& source);

  // Joins `group` (reference-counted) at `now` and emits an immediate state
  // change report plus a scheduled retransmission if this was a 0 -> 1 join.
  // Returns true if the set of joined groups changed.
  bool JoinGroup(const ::perception::network::IpAddress& group,
                 std::chrono::steady_clock::time_point now);

  // Leaves `group` (reference-counted) at `now` and emits a leave report if the
  // reference count reached 0. Returns true if the set of joined groups
  // changed.
  bool LeaveGroup(const ::perception::network::IpAddress& group,
                  std::chrono::steady_clock::time_point now);

  // Returns true if `group` is currently joined on this interface.
  bool IsJoined(const ::perception::network::IpAddress& group) const;

  // Returns all currently joined multicast groups and their reference counts.
  const std::map<::perception::network::IpAddress, int>& joined_groups() const {
    return joined_groups_;
  }

  // Processes a validated MLDv1 or MLDv2 Query at `now`.
  void OnQuery(const MldQuery& query,
               std::chrono::steady_clock::time_point now);

  // Advances timers at `now` and emits any due state-change retransmissions or
  // query responses.
  void OnTimer(std::chrono::steady_clock::time_point now);

  // Returns true if currently operating in MLDv1 compatibility mode at `now`.
  bool IsMldv1Mode(std::chrono::steady_clock::time_point now) const;

  // Returns the earliest pending timer deadline, or nullopt if idle.
  std::optional<std::chrono::steady_clock::time_point> NextDeadline() const;

 private:
  // Pending state-change or query response for a single group.
  struct PendingGroupAction {
    // Record type to send in MLDv2 mode.
    MldRecordType record_type = MldRecordType::ModeIsExclude;
    // Remaining transmissions (1 or 2).
    uint8 remaining_transmissions = 0;
    // Next transmission timestamp.
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::time_point::max();
  };

  // Sends an MLDv2 report containing `records` to ff02::16.
  void SendV2Report(const std::vector<MldAddressRecord>& records);

  // Sends an MLDv1 report or done message for `group`.
  void SendV1Message(const ::perception::network::IpAddress& group,
                     bool is_done);

  // Picks a delay in [0, max_ms] using `random_delay_` (defaults to max_ms / 2
  // when no random callback was supplied).
  std::chrono::milliseconds PickDelay(std::chrono::milliseconds max_delay);

  // Sink for outgoing IPv6 datagrams.
  Ipv6Sink send_sink_;
  // Optional random delay generator.
  MldRandomDelayFn random_delay_;
  // Link-local source address (or :: before link-local DAD completes).
  ::perception::network::IpAddress link_local_source_ =
      ::perception::network::IpAddress::V6Any();
  // Joined multicast groups with reference counts.
  std::map<::perception::network::IpAddress, int> joined_groups_;
  // Scheduled state-change or query-response transmissions per group.
  std::map<::perception::network::IpAddress, PendingGroupAction> pending_;
  // Deadline for a scheduled General Query response covering all joined groups.
  std::optional<std::chrono::steady_clock::time_point> general_query_deadline_;
  // Expiry of MLDv1 compatibility mode, if active.
  std::optional<std::chrono::steady_clock::time_point> mldv1_compat_until_;
};
