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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ipv6_header.h"
#include "perception/network/ip_address.h"

// A 6-byte Ethernet link-layer (MAC) address.
using HardwareAddress = std::array<uint8, 6>;

// Neighbor Discovery option types (RFC 4861 §4.6, RFC 8106, RFC 8781).
enum class NdpOptionType : uint8 {
  SourceLinkLayerAddress = 1,
  TargetLinkLayerAddress = 2,
  PrefixInformation = 3,
  RedirectedHeader = 4,
  Mtu = 5,
  RecursiveDnsServer = 25,
  DnsSearchList = 31,
  Nat64Prefix = 38,
};

// Prefix Information Option (RFC 4861 §4.6.2).
struct NdpPrefixInformation {
  // Number of leading bits in the prefix that are valid (0..128).
  uint8 prefix_length = 64;
  // On-link flag (L).
  bool on_link = false;
  // Autonomous address-configuration flag (A).
  bool autonomous = false;
  // Valid lifetime in seconds (0xFFFFFFFF = infinity).
  uint32 valid_lifetime_seconds = 0;
  // Preferred lifetime in seconds (0xFFFFFFFF = infinity).
  uint32 preferred_lifetime_seconds = 0;
  // Prefix address (bits beyond prefix_length should be zero).
  ::perception::network::IpAddress prefix;
};

// Recursive DNS Server Option (RFC 8106 §5.1).
struct NdpRdnssOption {
  // Lifetime in seconds (0 = stop using, 0xFFFFFFFF = infinity).
  uint32 lifetime_seconds = 0;
  // IPv6 addresses of recursive DNS servers.
  std::vector<::perception::network::IpAddress> servers;
};

// DNS Search List Option (RFC 8106 §5.2).
struct NdpDnsslOption {
  // Lifetime in seconds (0 = remove).
  uint32 lifetime_seconds = 0;
  // Decoded domain names in the search list.
  std::vector<std::string> domains;
};

// PREF64 NAT64 Prefix Option (RFC 8781 §4, option type 38).
struct NdpPref64Option {
  // Prefix length in bits (one of 96, 64, 56, 48, 40, or 32).
  uint8 prefix_length = 96;
  // Lifetime in seconds (rounded to a multiple of 8, max 65528).
  uint32 lifetime_seconds = 0;
  // The NAT64 prefix (top 96 bits populated, low 32 bits zero).
  ::perception::network::IpAddress prefix;
};

// Parsed Router Solicitation message (RFC 4861 §4.1).
struct NdpRouterSolicitation {
  // Optional Source Link-Layer Address option.
  std::optional<HardwareAddress> source_mac;
};

// Parsed Router Advertisement message (RFC 4861 §4.2).
struct NdpRouterAdvertisement {
  // Default CurHopLimit for outgoing unicast packets (0 = unspecified).
  uint8 cur_hop_limit = 0;
  // Managed address configuration flag (M) -> stateful DHCPv6.
  bool managed_flag = false;
  // Other configuration flag (O) -> stateless DHCPv6.
  bool other_config_flag = false;
  // Default router lifetime in seconds (0 = not a default router).
  uint16 router_lifetime_seconds = 0;
  // Time in milliseconds a neighbor is assumed reachable (0 = unspecified).
  uint32 reachable_time_ms = 0;
  // Time in milliseconds between retransmitted NS messages (0 = unspecified).
  uint32 retrans_timer_ms = 0;
  // Optional Source Link-Layer Address option.
  std::optional<HardwareAddress> source_mac;
  // Optional MTU option (values < 1280 are ignored during parsing).
  std::optional<uint32> mtu;
  // Prefix Information Options.
  std::vector<NdpPrefixInformation> prefixes;
  // Recursive DNS Server Options.
  std::vector<NdpRdnssOption> rdnss;
  // DNS Search List Options.
  std::vector<NdpDnsslOption> dnssl;
  // NAT64 PREF64 Options (RFC 8781).
  std::vector<NdpPref64Option> pref64;
};

// Parsed Neighbor Solicitation message (RFC 4861 §4.3).
struct NdpNeighborSolicitation {
  // Target address of the solicitation (never multicast).
  ::perception::network::IpAddress target;
  // Optional Source Link-Layer Address option.
  std::optional<HardwareAddress> source_mac;
};

// Parsed Neighbor Advertisement message (RFC 4861 §4.4).
struct NdpNeighborAdvertisement {
  // Router flag (R).
  bool router_flag = false;
  // Solicited flag (S).
  bool solicited_flag = false;
  // Override flag (O).
  bool override_flag = false;
  // Target address of the advertisement.
  ::perception::network::IpAddress target;
  // Optional Target Link-Layer Address option.
  std::optional<HardwareAddress> target_mac;
};

// Parsed Redirect message (RFC 4861 §4.5).
struct NdpRedirect {
  // Better first-hop target address.
  ::perception::network::IpAddress target;
  // Destination address being redirected.
  ::perception::network::IpAddress destination;
  // Optional Target Link-Layer Address option.
  std::optional<HardwareAddress> target_mac;
};

// Parses and validates a Router Solicitation (requires hop_limit == 255).
std::optional<NdpRouterSolicitation> ParseRouterSolicitation(
    const Ipv6Header& ipv6_header, std::string_view icmpv6_packet);

// Builds a Router Solicitation ICMPv6 payload with checksum.
std::string BuildRouterSolicitation(
    const ::perception::network::IpAddress& source,
    const ::perception::network::IpAddress& destination,
    const NdpRouterSolicitation& message);

// Parses and validates a Router Advertisement (requires hop_limit == 255 and a
// link-local source address).
std::optional<NdpRouterAdvertisement> ParseRouterAdvertisement(
    const Ipv6Header& ipv6_header, std::string_view icmpv6_packet);

// Builds a Router Advertisement ICMPv6 payload with checksum.
std::string BuildRouterAdvertisement(
    const ::perception::network::IpAddress& source,
    const ::perception::network::IpAddress& destination,
    const NdpRouterAdvertisement& message);

// Parses and validates a Neighbor Solicitation (requires hop_limit == 255).
std::optional<NdpNeighborSolicitation> ParseNeighborSolicitation(
    const Ipv6Header& ipv6_header, std::string_view icmpv6_packet);

// Builds a Neighbor Solicitation ICMPv6 payload with checksum.
std::string BuildNeighborSolicitation(
    const ::perception::network::IpAddress& source,
    const ::perception::network::IpAddress& destination,
    const NdpNeighborSolicitation& message);

// Parses and validates a Neighbor Advertisement (requires hop_limit == 255).
std::optional<NdpNeighborAdvertisement> ParseNeighborAdvertisement(
    const Ipv6Header& ipv6_header, std::string_view icmpv6_packet);

// Builds a Neighbor Advertisement ICMPv6 payload with checksum.
std::string BuildNeighborAdvertisement(
    const ::perception::network::IpAddress& source,
    const ::perception::network::IpAddress& destination,
    const NdpNeighborAdvertisement& message);

// Parses and validates a Redirect message (requires hop_limit == 255 and a
// link-local source address).
std::optional<NdpRedirect> ParseRedirect(const Ipv6Header& ipv6_header,
                                         std::string_view icmpv6_packet);

// Builds a Redirect ICMPv6 payload with checksum.
std::string BuildRedirect(const ::perception::network::IpAddress& source,
                          const ::perception::network::IpAddress& destination,
                          const NdpRedirect& message);

// Neighbor Unreachability Detection states (RFC 4861 §7.3.2).
enum class NudState {
  Incomplete,
  Reachable,
  Stale,
  Delay,
  Probe,
};

// State of a single neighbor in the NUD state machine.
struct NudEntry {
  // Neighbor's IPv6 address.
  ::perception::network::IpAddress address;
  // Link-layer address (valid in all states except Incomplete).
  HardwareAddress mac{};
  // Reachability state.
  NudState state = NudState::Incomplete;
  // True if the neighbor is known to be a router.
  bool is_router = false;
  // Number of solicitations sent in Incomplete or Probe state.
  uint8 probes_sent = 0;
  // Deadline for the current state transition or retransmission.
  std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::time_point::max();
};

// Pure RFC 4861 Neighbor Unreachability Detection (NUD) state machine.
class NudStateMachine {
 public:
  // Default BaseReachableTime (30 seconds, RFC 4861 §10).
  static constexpr auto kDefaultReachableTime = std::chrono::milliseconds(30000);

  // Default RetransTimer (1 second, RFC 4861 §10).
  static constexpr auto kDefaultRetransTimer = std::chrono::milliseconds(1000);

  // Delay before sending the first probe after entering Delay (5 seconds).
  static constexpr auto kDelayFirstProbeTime = std::chrono::milliseconds(5000);

  // Maximum multicast solicitations in Incomplete state.
  static constexpr uint8 kMaxMulticastSolicit = 3;

  // Maximum unicast probes in Probe state.
  static constexpr uint8 kMaxUnicastSolicit = 3;

  NudStateMachine(const ::perception::network::IpAddress& local_source,
                  const HardwareAddress& local_mac, Ipv6Sink send_sink);

  // Updates local source address and hardware address.
  void SetLocalIdentity(const ::perception::network::IpAddress& local_source,
                        const HardwareAddress& local_mac);

  // Updates link timers advertised in a Router Advertisement.
  void SetTimers(std::chrono::milliseconds reachable_time,
                 std::chrono::milliseconds retrans_timer);

  // Looks up an entry by IPv6 address.
  const NudEntry* Find(const ::perception::network::IpAddress& neighbor) const;

  // Starts resolution or notes traffic sent to `neighbor` at `now`. If the
  // neighbor is already resolved, returns its MAC address (transitioning Stale
  // -> Delay if needed). If not yet resolved, starts Incomplete state and emits
  // a multicast Neighbor Solicitation.
  std::optional<HardwareAddress> ResolveOrTouch(
      const ::perception::network::IpAddress& neighbor,
      std::chrono::steady_clock::time_point now);

  // Records a link-layer address learned from an NS SLLAO, RS SLLAO, RA SLLAO,
  // or Redirect TLLAO (RFC 4861 §7.2.3).
  void RecordPassiveLinkLayerAddress(
      const ::perception::network::IpAddress& neighbor,
      const HardwareAddress& mac, bool is_router,
      std::chrono::steady_clock::time_point now);

  // Processes a verified Neighbor Advertisement (RFC 4861 §7.2.5).
  void OnNeighborAdvertisement(const NdpNeighborAdvertisement& advertisement,
                               std::chrono::steady_clock::time_point now);

  // Records positive upper-layer reachability confirmation (e.g. TCP ACK
  // advance, RFC 4861 §7.3.1).
  void ConfirmReachability(const ::perception::network::IpAddress& neighbor,
                           std::chrono::steady_clock::time_point now);

  // Advances timers at `now`, sending retransmitted NS probes or expiring
  // unreachable entries.
  void OnTimer(std::chrono::steady_clock::time_point now);

  // Returns the earliest deadline across all entries, if any.
  std::optional<std::chrono::steady_clock::time_point> NextDeadline() const;

 private:
  // Emits a multicast or unicast Neighbor Solicitation for `target`.
  void SendSolicitation(const ::perception::network::IpAddress& target,
                        bool unicast, const HardwareAddress& unused_mac);

  // Local source address used for outgoing Neighbor Solicitations.
  ::perception::network::IpAddress local_source_;
  // Local Ethernet MAC address included in SLLAO.
  HardwareAddress local_mac_{};
  // Sink receiving generated Neighbor Solicitation datagrams.
  Ipv6Sink send_sink_;
  // Reachable duration for Reachable state.
  std::chrono::milliseconds reachable_time_ = kDefaultReachableTime;
  // Retransmission interval for Incomplete and Probe states.
  std::chrono::milliseconds retrans_timer_ = kDefaultRetransTimer;
  // Per-neighbor state table.
  std::map<::perception::network::IpAddress, NudEntry> entries_;
};

// Configuration for per-interface Router Advertisement transmission (RFC 4861
// §6.2, user decision Q24).
struct RouterAdvertisementConfig {
  // Enables router mode (periodic RAs and RS replies) on the interface.
  bool enabled = false;
  // Link-local IPv6 source address of the router interface.
  ::perception::network::IpAddress link_local_source;
  // MAC address of the router interface (advertised in SLLAO).
  HardwareAddress source_mac{};
  // Advertised CurHopLimit.
  uint8 cur_hop_limit = 64;
  // Managed flag (M).
  bool managed_flag = false;
  // Other configuration flag (O).
  bool other_config_flag = false;
  // Router lifetime in seconds.
  uint16 router_lifetime_seconds = 1800;
  // Advertised ReachableTime in milliseconds.
  uint32 reachable_time_ms = 0;
  // Advertised RetransTimer in milliseconds.
  uint32 retrans_timer_ms = 0;
  // Optional advertised link MTU.
  std::optional<uint32> mtu;
  // Advertised Prefix Information Options (PIO).
  std::vector<NdpPrefixInformation> prefixes;
  // Advertised Recursive DNS Server Options (RDNSS).
  std::vector<NdpRdnssOption> rdnss;
  // Advertised DNS Search List Options (DNSSL).
  std::vector<NdpDnsslOption> dnssl;
  // Advertised NAT64 PREF64 Options.
  std::vector<NdpPref64Option> pref64;
  // Periodic advertisement interval (default 200 seconds).
  std::chrono::seconds adv_interval{200};
};

// Per-interface RFC 4861 §6.2 Router Advertisement sender. When enabled, sends
// initial and periodic unsolicited RAs to ff02::1 and responds to incoming
// Router Solicitations.
class RouterAdvertiser {
 public:
  // Maximum initial rapid Router Advertisements on enable (RFC 4861 §6.2.4).
  static constexpr uint8 kMaxInitialRtrAdvertisements = 3;

  // Interval between initial Router Advertisements (16 seconds).
  static constexpr auto kMaxInitialRtrAdvertInterval = std::chrono::seconds(16);

  // Minimum interval between multicast Router Advertisements (3 seconds).
  static constexpr auto kMinDelayBetweenRas = std::chrono::seconds(3);

  explicit RouterAdvertiser(Ipv6Sink send_sink);

  // Applies `config` at `now`. If `config.enabled` is true, transmits the first
  // unsolicited RA immediately and schedules the remaining initial/periodic RAs.
  void SetConfig(const RouterAdvertisementConfig& config,
                 std::chrono::steady_clock::time_point now);

  // Returns the current configuration.
  const RouterAdvertisementConfig& config() const { return config_; }

  // Handles an incoming validated Router Solicitation at `now`.
  void OnRouterSolicitation(const Ipv6Header& ipv6_header,
                            const NdpRouterSolicitation& solicitation,
                            std::chrono::steady_clock::time_point now);

  // Advances timers at `now` and sends any due unsolicited RA.
  void OnTimer(std::chrono::steady_clock::time_point now);

  // Returns the next scheduled RA transmission time, or nullopt when disabled.
  std::optional<std::chrono::steady_clock::time_point> NextDeadline() const;

 private:
  // Emits a Router Advertisement to `destination` at `now`.
  void SendAdvertisement(const ::perception::network::IpAddress& destination,
                         std::chrono::steady_clock::time_point now);

  // Sink for outgoing IPv6 datagrams.
  Ipv6Sink send_sink_;
  // Active router advertisement configuration.
  RouterAdvertisementConfig config_;
  // Remaining initial rapid advertisements to send.
  uint8 initial_advertisements_remaining_ = 0;
  // Timestamp of the last multicast RA sent.
  std::optional<std::chrono::steady_clock::time_point> last_multicast_ra_;
  // Next scheduled unsolicited RA deadline.
  std::optional<std::chrono::steady_clock::time_point> next_ra_deadline_;
};
