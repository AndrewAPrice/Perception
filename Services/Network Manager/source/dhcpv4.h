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
#include <string>
#include <string_view>
#include <vector>

#include "interface_address.h"
#include "ndp.h"
#include "perception/network/ip_address.h"

// UDP port on which DHCPv4 clients listen and send (RFC 2131 §4.1).
inline constexpr uint16 kDhcpv4ClientPort = 68;

// UDP port on which DHCPv4 servers and relay agents listen (RFC 2131 §4.1).
inline constexpr uint16 kDhcpv4ServerPort = 67;

// DHCPv4 magic cookie following the 236-byte BOOTP header (RFC 2132 §2).
inline constexpr uint32 kDhcpv4MagicCookie = 0x63825363u;

// Broadcast flag in the BOOTP flags field (RFC 2131 §2).
inline constexpr uint16 kDhcpv4BroadcastFlag = 0x8000u;

// DHCPv4 message types carried in Option 53 (RFC 2132 §9.6).
enum class Dhcpv4MessageType : uint8 {
  Discover = 1,
  Offer = 2,
  Request = 3,
  Decline = 4,
  Ack = 5,
  Nak = 6,
  Release = 7,
  Inform = 8,
};

// DHCPv4 option codes (RFC 2132).
enum class Dhcpv4OptionCode : uint8 {
  Pad = 0,
  SubnetMask = 1,
  Router = 3,
  DnsServers = 6,
  RequestedIpAddress = 50,
  IpAddressLeaseTime = 51,
  MessageType = 53,
  ServerIdentifier = 54,
  ParameterRequestList = 55,
  RenewalTimeValue = 58,
  RebindingTimeValue = 59,
  ClientIdentifier = 61,
  End = 255,
};

// Parsed or outgoing DHCPv4 message (RFC 2131 / RFC 2132).
struct Dhcpv4Message {
  // BOOTP op code (1 = BOOTREQUEST, 2 = BOOTREPLY).
  uint8 op = 1;
  // 32-bit transaction ID (xid).
  uint32 transaction_id = 0;
  // Seconds elapsed since the client began the acquisition/renewal process.
  uint16 seconds_elapsed = 0;
  // BOOTP flags (e.g., kDhcpv4BroadcastFlag).
  uint16 flags = 0;
  // Client IP address (ciaddr), populated in Bound/Renew/Rebind states.
  ::perception::network::IpAddress client_ip =
      ::perception::network::IpAddress::V4Any();
  // 'Your' (client) IP address offered or assigned by the server (yiaddr).
  ::perception::network::IpAddress your_ip =
      ::perception::network::IpAddress::V4Any();
  // Next server IP address in bootstrap (siaddr).
  ::perception::network::IpAddress next_server_ip =
      ::perception::network::IpAddress::V4Any();
  // Client link-layer (MAC) address (chaddr[0..5]).
  HardwareAddress client_mac{};
  // DHCP message type (Option 53).
  Dhcpv4MessageType type = Dhcpv4MessageType::Discover;
  // Subnet mask (Option 1).
  std::optional<::perception::network::IpAddress> subnet_mask;
  // Default routers in preference order (Option 3).
  std::vector<::perception::network::IpAddress> routers;
  // Recursive DNS servers (Option 6).
  std::vector<::perception::network::IpAddress> dns_servers;
  // Requested IPv4 address (Option 50).
  std::optional<::perception::network::IpAddress> requested_ip;
  // IP address lease time in seconds (Option 51).
  std::optional<uint32> lease_time_seconds;
  // DHCP server identifier (Option 54).
  std::optional<::perception::network::IpAddress> server_id;
  // Parameter request list of option codes (Option 55).
  std::vector<uint8> parameter_request_list;
  // Renewal (T1) time value in seconds (Option 58).
  std::optional<uint32> t1_seconds;
  // Rebinding (T2) time value in seconds (Option 59).
  std::optional<uint32> t2_seconds;
};

// Parses and bounds-checks a DHCPv4 UDP payload.
std::optional<Dhcpv4Message> ParseDhcpv4Message(std::string_view udp_payload);

// Serializes a DHCPv4 message into a UDP payload.
std::string BuildDhcpv4Message(const Dhcpv4Message& message);

// Converts an IPv4 subnet mask (e.g., 255.255.255.0) to a prefix length (24).
uint8 SubnetMaskToPrefixLength(
    const ::perception::network::IpAddress& subnet_mask);

// Maps a 32-bit random value into the usable RFC 3927 §2.1 IPv4 link-local
// range (169.254.1.0 through 169.254.254.255 inclusive, excluding the first
// and last 256 addresses).
::perception::network::IpAddress SelectIpv4LinkLocalCandidate(
    uint32 random_value);

// Lifecycle state of the DHCPv4 + RFC 3927 Link-Local client.
enum class Dhcpv4State {
  Idle,
  Selecting,
  Requesting,
  Bound,
  Renewing,
  Rebinding,
  LinkLocalProbing,
  LinkLocalBound,
};

// Pure DHCPv4 (RFC 2131) + IPv4 Link-Local (RFC 3927) client state machine.
class Dhcpv4Client {
 public:
  // Callback used to send a DHCPv4 UDP payload from `src_ip`:68 to `dst_ip`:67.
  using Dhcpv4SendFn = std::function<void(
      std::string payload, const ::perception::network::IpAddress& src_ip,
      const ::perception::network::IpAddress& dst_ip)>;

  // Callback used to transmit an ARP request/announcement with `sender_ip` and
  // `target_ip` (for RFC 3927 §2.2.1 ARP probing, `sender_ip` is 0.0.0.0).
  using ArpProbeFn = std::function<void(
      const ::perception::network::IpAddress& sender_ip,
      const ::perception::network::IpAddress& target_ip)>;

  // Callback returning a 32-bit pseudo-random value for xid and link-local
  // address selection.
  using Random32Fn = std::function<uint32()>;

  // Initial retransmission timeout for Discover/Request (4 seconds, RFC 2131 §4.1).
  static constexpr auto kInitialRetransmitTimeout =
      std::chrono::milliseconds(4000);

  // Maximum retransmission timeout for Discover/Request (64 seconds, RFC 2131 §4.1).
  static constexpr auto kMaxRetransmitTimeout =
      std::chrono::milliseconds(64000);

  // Number of unanswered DHCPDISCOVER transmissions before falling back to
  // RFC 3927 169.254.0.0/16 link-local probing while continuing background
  // DHCPDISCOVER retries.
  static constexpr uint8 kDiscoverAttemptsBeforeLinkLocal = 3;

  // Number of RFC 3927 ARP probes before claiming a link-local address (PROBE_NUM = 3).
  static constexpr uint8 kLinkLocalProbeCount = 3;

  // Interval between RFC 3927 ARP probes and announcement wait (1 second).
  static constexpr auto kLinkLocalProbeInterval =
      std::chrono::milliseconds(1000);

  // Background DHCPDISCOVER interval while operating on an RFC 3927 link-local
  // address (30 seconds).
  static constexpr auto kBackgroundDiscoverInterval =
      std::chrono::milliseconds(30000);

  Dhcpv4Client(const HardwareAddress& mac, Dhcpv4SendFn send_fn,
               ArpProbeFn arp_probe_fn = {}, Random32Fn random_fn = {});

  // Starts DHCPv4 discovery at `now`.
  void Start(std::chrono::steady_clock::time_point now);

  // Releases any active DHCPv4 lease and returns to Idle.
  void Release(std::chrono::steady_clock::time_point now);

  // Processes an incoming DHCPv4 UDP payload at `now`.
  void OnPacket(std::string_view udp_payload,
                std::chrono::steady_clock::time_point now);

  // Processes an incoming ARP packet's sender IP, target IP (for probe
  // conflict), and sender MAC for RFC 3927 conflict detection at `now`.
  void OnArpPacket(const ::perception::network::IpAddress& sender_ip,
                   const ::perception::network::IpAddress& target_ip,
                   const HardwareAddress& sender_mac,
                   std::chrono::steady_clock::time_point now);

  // Advances retransmission, T1/T2, lease expiry, and RFC 3927 ARP probe
  // timers at `now`.
  void OnTimer(std::chrono::steady_clock::time_point now);

  // Returns the current client state.
  Dhcpv4State state() const { return state_; }

  // Returns the active interface address (either a Bound/Renewing/Rebinding
  // DHCPv4 lease or a LinkLocalBound 169.254.x.y/16 address).
  const std::optional<InterfaceAddress>& address() const { return address_; }

  // Returns the leased subnet mask, or 255.255.0.0 when LinkLocalBound.
  std::optional<::perception::network::IpAddress> subnet_mask() const {
    return subnet_mask_;
  }

  // Returns the primary default router learned from Option 3, if any.
  std::optional<::perception::network::IpAddress> default_router() const {
    return default_router_;
  }

  // Returns the recursive DNS servers learned from Option 6.
  const std::vector<::perception::network::IpAddress>& dns_servers() const {
    return dns_servers_;
  }

  // Returns the selected DHCPv4 server identifier.
  std::optional<::perception::network::IpAddress> server_id() const {
    return server_id_;
  }

  // Returns the earliest timer deadline, or nullopt if Idle.
  std::optional<std::chrono::steady_clock::time_point> NextDeadline() const;

 private:
  // Allocates a fresh 32-bit transaction ID.
  uint32 NextRandom32();

  // Sends a DHCPDISCOVER message at `now`.
  void SendDiscover(std::chrono::steady_clock::time_point now);

  // Sends a DHCPREQUEST message at `now`.
  void SendRequest(std::chrono::steady_clock::time_point now);

  // Starts or restarts RFC 3927 link-local ARP probing with a new candidate.
  void StartLinkLocalProbing(std::chrono::steady_clock::time_point now);

  // Applies a verified DHCPACK lease at `now`.
  void ApplyAck(const Dhcpv4Message& ack,
                std::chrono::steady_clock::time_point now);

  // Client MAC address.
  HardwareAddress mac_{};
  // Outgoing DHCPv4 UDP sender.
  Dhcpv4SendFn send_fn_;
  // Outgoing ARP probe/announcement sender.
  ArpProbeFn arp_probe_fn_;
  // Random 32-bit generator.
  Random32Fn random_fn_;
  // Fallback PRNG state when no external generator is provided.
  uint32 prng_state_ = 0;
  // Current state.
  Dhcpv4State state_ = Dhcpv4State::Idle;
  // Active 32-bit transaction ID.
  uint32 active_xid_ = 0;
  // Timestamp when the current exchange started.
  std::chrono::steady_clock::time_point exchange_start_{};
  // Current retransmission interval.
  std::chrono::milliseconds current_rto_ = kInitialRetransmitTimeout;
  // Number of transmissions in the current exchange.
  uint8 attempts_ = 0;
  // Next DHCPv4 retransmission or background-discover deadline.
  std::optional<std::chrono::steady_clock::time_point> retransmit_deadline_;
  // Offered IPv4 address during Requesting.
  ::perception::network::IpAddress offered_ip_ =
      ::perception::network::IpAddress::V4Any();
  // Selected DHCPv4 server identifier.
  std::optional<::perception::network::IpAddress> server_id_;
  // Active assigned address (DHCPv4 or LinkLocal).
  std::optional<InterfaceAddress> address_;
  // Active subnet mask.
  std::optional<::perception::network::IpAddress> subnet_mask_;
  // Active default router.
  std::optional<::perception::network::IpAddress> default_router_;
  // Active recursive DNS servers.
  std::vector<::perception::network::IpAddress> dns_servers_;
  // T1 (Renew) deadline.
  std::optional<std::chrono::steady_clock::time_point> t1_deadline_;
  // T2 (Rebind) deadline.
  std::optional<std::chrono::steady_clock::time_point> t2_deadline_;
  // Candidate RFC 3927 169.254.x.y address being probed.
  ::perception::network::IpAddress link_local_candidate_ =
      ::perception::network::IpAddress::V4Any();
  // Number of ARP probes sent for `link_local_candidate_`.
  uint8 link_local_probes_sent_ = 0;
  // Next RFC 3927 ARP probe or claim deadline.
  std::optional<std::chrono::steady_clock::time_point> link_local_deadline_;
};
