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

// UDP port on which DHCPv6 clients listen and send (RFC 8415 §7.2).
inline constexpr uint16 kDhcpv6ClientPort = 546;

// UDP port on which DHCPv6 servers and relay agents listen (RFC 8415 §7.2).
inline constexpr uint16 kDhcpv6ServerPort = 547;

// Debug knob that forces a stateless DHCPv6 Information-Request even when a
// Router Advertisement carries M=0 and O=0 (exercises slirp's DHCPv6 server).
inline constexpr bool kForceDhcpv6InformationRequest = false;

// Default Information Refresh Time (24 hours, RFC 8415 §7.6).
inline constexpr uint32 kDefaultInformationRefreshSeconds = 86400;

// Minimum Information Refresh Time (600 seconds, RFC 8415 §7.6).
inline constexpr uint32 kMinimumInformationRefreshSeconds = 600;

// DHCPv6 message types (RFC 8415 §7.3).
enum class Dhcpv6MessageType : uint8 {
  Solicit = 1,
  Advertise = 2,
  Request = 3,
  Confirm = 4,
  Renew = 5,
  Rebind = 6,
  Reply = 7,
  Release = 8,
  Decline = 9,
  Reconfigure = 10,
  InformationRequest = 11,
};

// DHCPv6 option codes (RFC 8415 §21, RFC 3646).
enum class Dhcpv6OptionCode : uint16 {
  ClientId = 1,
  ServerId = 2,
  IaNa = 3,
  IaAddress = 5,
  OptionRequest = 6,
  Preference = 7,
  ElapsedTime = 8,
  StatusCode = 13,
  DnsServers = 23,
  DomainList = 24,
  InformationRefreshTime = 32,
};

// DHCPv6 status codes (RFC 8415 §21.13).
enum class Dhcpv6StatusCode : uint16 {
  Success = 0,
  UnspecFail = 1,
  NoAddrsAvail = 2,
  NoBinding = 3,
  NotOnLink = 4,
  UseMulticast = 5,
};

// An IA Address option inside an IA_NA option (RFC 8415 §21.6).
struct Dhcpv6IaAddress {
  // Assigned IPv6 address.
  ::perception::network::IpAddress address;
  // Preferred lifetime in seconds.
  uint32 preferred_lifetime_seconds = 0;
  // Valid lifetime in seconds.
  uint32 valid_lifetime_seconds = 0;
};

// An Identity Association for Non-temporary Addresses option (RFC 8415 §21.4).
struct Dhcpv6IaNa {
  // Unique identifier for this IA_NA on the interface.
  uint32 iaid = 0;
  // Renewal time in seconds from the server (0 = client chooses).
  uint32 t1_seconds = 0;
  // Rebinding time in seconds from the server (0 = client chooses).
  uint32 t2_seconds = 0;
  // Optional nested status code inside the IA_NA.
  uint16 status_code = 0;
  // Addresses carried in this IA_NA.
  std::vector<Dhcpv6IaAddress> addresses;
};

// Parsed or outgoing DHCPv6 message.
struct Dhcpv6Message {
  // Message type (1..11).
  Dhcpv6MessageType type = Dhcpv6MessageType::Solicit;
  // 24-bit transaction ID.
  uint32 transaction_id = 0;
  // Client DUID (Option 1).
  std::string client_id;
  // Server DUID (Option 2).
  std::string server_id;
  // Elapsed time in hundredths of a second (Option 8).
  std::optional<uint16> elapsed_time_centiseconds;
  // Option codes requested in Option Request Option (Option 6).
  std::vector<uint16> requested_options;
  // Server preference value (Option 7).
  std::optional<uint8> preference;
  // Top-level Status Code (Option 13).
  std::optional<uint16> status_code;
  // IA_NA options (Option 3).
  std::vector<Dhcpv6IaNa> ia_nas;
  // Recursive DNS servers (Option 23, RFC 3646).
  std::vector<::perception::network::IpAddress> dns_servers;
  // Domain search list (Option 24, RFC 3646).
  std::vector<std::string> domain_search_list;
  // Information Refresh Time in seconds (Option 32).
  std::optional<uint32> info_refresh_time_seconds;
};

// Builds a Link-Layer DUID (DUID-LL, type 3, hardware type 1 Ethernet) from
// `mac` (RFC 8415 §11.4).
std::string BuildDuidLinkLayer(const HardwareAddress& mac);

// Parses and bounds-checks a DHCPv6 UDP payload.
std::optional<Dhcpv6Message> ParseDhcpv6Message(std::string_view udp_payload);

// Serializes a DHCPv6 message into a UDP payload.
std::string BuildDhcpv6Message(const Dhcpv6Message& message);

// Callback used by Dhcpv6Client to transmit a DHCPv6 UDP payload (from UDP
// port 546 to ff02::1:2 port 547).
using Dhcpv6SendFn = std::function<void(std::string payload)>;

// Callback returning a random 24-bit transaction ID.
using Dhcpv6TransactionIdFn = std::function<uint32()>;

// Lifecycle state of the DHCPv6 client.
enum class Dhcpv6State {
  Idle,
  StatelessRequesting,
  StatelessBound,
  Soliciting,
  Requesting,
  Bound,
  Renewing,
  Rebinding,
};

// Pure RFC 8415 stateless and stateful DHCPv6 client state machine.
class Dhcpv6Client {
 public:
  // Initial retransmission timeout for Solicit/Information-Request/Request (1 s).
  static constexpr auto kInitialRetransmitTimeout =
      std::chrono::milliseconds(1000);

  // Initial retransmission timeout for Renew/Rebind (10 s, RFC 8415 §7.6).
  static constexpr auto kRenewInitialRetransmitTimeout =
      std::chrono::milliseconds(10000);

  // Maximum retransmission timeout for Solicit/Information-Request (3600 s).
  static constexpr auto kMaxSolicitRetransmitTimeout =
      std::chrono::milliseconds(3600000);

  // Maximum retransmission timeout for Request (30 s, RFC 8415 §7.6).
  static constexpr auto kMaxRequestRetransmitTimeout =
      std::chrono::milliseconds(30000);

  // Maximum retransmission count for Request (10, RFC 8415 §7.6).
  static constexpr uint8 kMaxRequestRetries = 10;

  Dhcpv6Client(const HardwareAddress& mac, Dhcpv6SendFn send_fn,
               Dhcpv6TransactionIdFn tx_id_fn = {});

  // Reacts to the M and O flags of a received Router Advertisement at `now`.
  void OnRouterAdvertisementFlags(bool managed_flag, bool other_config_flag,
                                  std::chrono::steady_clock::time_point now);

  // Starts stateless configuration (Information-Request -> Reply) at `now`.
  void StartStateless(std::chrono::steady_clock::time_point now);

  // Starts stateful IA_NA configuration (Solicit -> Advertise -> Request ->
  // Reply) at `now`.
  void StartStateful(std::chrono::steady_clock::time_point now);

  // Sends a Release message for any active stateful lease and returns to Idle.
  void Release(std::chrono::steady_clock::time_point now);

  // Processes an incoming DHCPv6 UDP payload at `now`.
  void OnPacket(std::string_view udp_payload,
                std::chrono::steady_clock::time_point now);

  // Advances retransmission, T1/T2, refresh, and lease expiry timers at `now`.
  void OnTimer(std::chrono::steady_clock::time_point now);

  // Returns the current client state.
  Dhcpv6State state() const { return state_; }

  // Returns the client DUID-LL.
  const std::string& client_duid() const { return client_duid_; }

  // Returns the IAID used for IA_NA requests.
  uint32 iaid() const { return iaid_; }

  // Returns the addresses leased via stateful IA_NA (/128 prefix length).
  const std::vector<InterfaceAddress>& addresses() const { return addresses_; }

  // Returns the DNS servers learned from DHCPv6.
  const std::vector<::perception::network::IpAddress>& dns_servers() const {
    return dns_servers_;
  }

  // Returns the earliest timer deadline, or nullopt if Idle.
  std::optional<std::chrono::steady_clock::time_point> NextDeadline() const;

 private:
  // Allocates a fresh 24-bit transaction ID.
  uint32 NextTransactionId();

  // Transmits the current message and schedules its retransmission deadline.
  void SendCurrentExchangeMessage(std::chrono::steady_clock::time_point now);

  // Applies an IA_NA and DNS configuration from a verified Reply at `now`.
  void ApplyStatefulReply(const Dhcpv6Message& reply, const Dhcpv6IaNa& ia,
                          std::chrono::steady_clock::time_point now);

  // Client DUID-LL.
  std::string client_duid_;
  // IAID derived from the low 4 bytes of the MAC address.
  uint32 iaid_ = 1;
  // Outgoing UDP payload callback.
  Dhcpv6SendFn send_fn_;
  // Transaction ID generator.
  Dhcpv6TransactionIdFn tx_id_fn_;
  // Fallback counter for transaction IDs when no generator is supplied.
  uint32 fallback_tx_id_ = 0x102030;
  // Current client state.
  Dhcpv6State state_ = Dhcpv6State::Idle;
  // Active 24-bit transaction ID.
  uint32 active_tx_id_ = 0;
  // Start timestamp of the current message exchange (for Elapsed Time option).
  std::chrono::steady_clock::time_point exchange_start_{};
  // Current retransmission interval.
  std::chrono::milliseconds current_rto_ = kInitialRetransmitTimeout;
  // Number of retransmissions sent in the current exchange.
  uint8 retries_ = 0;
  // Next retransmission or state-transition deadline.
  std::optional<std::chrono::steady_clock::time_point> retransmit_deadline_;
  // Server DUID selected from Advertise or Bound lease.
  std::string server_duid_;
  // T1 (Renew) timestamp for stateful leases.
  std::optional<std::chrono::steady_clock::time_point> t1_deadline_;
  // T2 (Rebind) timestamp for stateful leases.
  std::optional<std::chrono::steady_clock::time_point> t2_deadline_;
  // Refresh timestamp for stateless configuration.
  std::optional<std::chrono::steady_clock::time_point> refresh_deadline_;
  // Leased stateful addresses (/128).
  std::vector<InterfaceAddress> addresses_;
  // Learned recursive DNS servers.
  std::vector<::perception::network::IpAddress> dns_servers_;
};
