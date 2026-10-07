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
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "ipv6_header.h"
#include "perception/network/ip_address.h"

// IPv6 Next Header number for the Mobility Header (RFC 6275 §6.1).
inline constexpr uint8 kIpProtocolMobility = 135;

// IPv6 Next Header number for IPv6-in-IPv6 encapsulation (RFC 2473 / RFC 6275).
inline constexpr uint8 kIpProtocolIpv6Encap = 41;

// Destination Option type for the Home Address option (RFC 6275 §6.3).
inline constexpr uint8 kHomeAddressOptionType = 201;

// Routing Header type for Mobile IPv6 Type 2 Routing Header (RFC 6275 §6.4).
inline constexpr uint8 kType2RoutingHeaderType = 2;

// Mobility Header message types (RFC 6275 §6.1.1).
enum class MobilityHeaderType : uint8 {
  BindingRefreshRequest = 0,
  HomeTestInit = 1,
  CareOfTestInit = 2,
  HomeTest = 3,
  CareOfTest = 4,
  BindingUpdate = 5,
  BindingAcknowledgement = 6,
  BindingError = 7,
};

// Binding Acknowledgement status codes (RFC 6275 §6.1.8).
enum class BindingAckStatus : uint8 {
  Accepted = 0,
  AcceptedNeedsPrefixDiscovery = 1,
  ReasonUnspecified = 128,
  AdministrativelyProhibited = 129,
  InsufficientResources = 130,
  HomeRegistrationNotSupported = 131,
  NotHomeSubnet = 132,
  NotHomeAgentForThisMobileNode = 133,
  DuplicateAddressDetectionFailed = 134,
  SequenceNumberOutOfWindow = 135,
  ExpiredHomeNonceIndex = 136,
  ExpiredCareOfNonceIndex = 137,
  ExpiredNonces = 138,
  RegistrationTypeChangeDisallowed = 139,
};

// Binding Error status codes (RFC 6275 §6.1.9).
enum class BindingErrorStatus : uint8 {
  UnknownBindingForHomeAddressOption = 1,
  UnrecognizedMobilityHeaderType = 2,
};

// Parsed or serializable Mobility Header message (RFC 6275 §6.1).
struct MobilityMessage {
  // Mobility Header message type.
  MobilityHeaderType type = MobilityHeaderType::BindingRefreshRequest;
  // 8-byte init cookie (used by HoTI, CoTI, HoT, CoT).
  std::array<uint8, 8> init_cookie{};
  // 8-byte keygen token (used by HoT, CoT).
  std::array<uint8, 8> keygen_token{};
  // Nonce index (used by HoT, CoT).
  uint16 nonce_index = 0;
  // Sequence number (used by BU, BA).
  uint16 sequence_number = 0;
  // Lifetime in units of 4 seconds (used by BU, BA).
  uint16 lifetime_units = 0;
  // Binding Update / Acknowledgement flags.
  bool acknowledge_requested = false;
  bool home_registration = false;
  bool link_local_compatibility = false;
  bool key_management_mobility = false;
  // Status code (used by BA, BE).
  uint8 status = 0;
  // Home address field in a Binding Error message.
  ::perception::network::IpAddress error_home_address;
  // Optional Alternate Care-of Address option (type 3).
  std::optional<::perception::network::IpAddress> alternate_care_of_address;
  // Optional Nonce Indices option (type 4): (home_index, care_of_index).
  std::optional<std::pair<uint16, uint16>> nonce_indices;
  // Optional 12-byte Binding Authorization Data authenticator (type 5).
  std::optional<std::array<uint8, 12>> authenticator;
  // Optional Binding Refresh Advice interval in 4-second units (type 2).
  std::optional<uint16> refresh_advice_units;
};

// Serializes `message` into a Mobility Header wire payload and computes its
// ICMPv6-style pseudo-header checksum (Next Header 135). If `k_bm` is provided
// and `message.authenticator` is engaged, computes the 96-bit HMAC-SHA1
// authenticator over `(care_of_address, cn_address, mobility_header)`.
std::string SerializeMobilityHeader(
    const ::perception::network::IpAddress& checksum_source,
    const ::perception::network::IpAddress& checksum_destination,
    const MobilityMessage& message,
    const std::optional<std::array<uint8, 20>>& k_bm = std::nullopt,
    const ::perception::network::IpAddress& auth_care_of_address = {},
    const ::perception::network::IpAddress& auth_cn_address = {});

// Parses and validates a Mobility Header wire payload (including its checksum
// over `checksum_source` and `checksum_destination`).
std::optional<MobilityMessage> ParseMobilityHeader(
    const ::perception::network::IpAddress& checksum_source,
    const ::perception::network::IpAddress& checksum_destination,
    std::string_view payload);

// Verifies the Binding Authorization Data option on a raw Mobility Header using
// the 20-byte binding management key `k_bm`.
bool VerifyMobilityHeaderAuthenticator(
    std::string_view raw_mobility_header, const std::array<uint8, 20>& k_bm,
    const ::perception::network::IpAddress& care_of_address,
    const ::perception::network::IpAddress& cn_address);

// Derives the 20-byte Binding Management Key K_bm = SHA1(home_token | coa_token)
// per RFC 6275 §5.2.5.
std::array<uint8, 20> DeriveBindingManagementKey(
    const std::array<uint8, 8>& home_keygen_token,
    const std::array<uint8, 8>& care_of_keygen_token);

// Builds a 24-byte Destination Options extension header containing the
// 16-byte Home Address option (type 201) aligned to 8n + 6.
std::string BuildHomeAddressDestinationOptionsHeader(
    uint8 next_header, const ::perception::network::IpAddress& home_address);

// Extracts the Home Address from a Destination Options extension header body
// (excluding the first 2 bytes `next_header` and `hdr_ext_len`).
std::optional<::perception::network::IpAddress>
ParseHomeAddressDestinationOption(std::string_view options_body);

// Builds a 24-byte Type 2 Routing Header (Next Header 43, Routing Type 2,
// Segments Left = 1) carrying `home_address` per RFC 6275 §6.4.
std::string BuildType2RoutingHeader(
    uint8 next_header, const ::perception::network::IpAddress& home_address);

// Result of inspecting or processing a Routing Header on an IPv6 packet.
enum class RoutingHeaderStatus : uint8 {
  // Packet had a valid Type 2 Routing Header for `local_home_address`; the
  // outer destination was swapped to `local_home_address` and Segments Left
  // was decremented to 0.
  ProcessedType2,
  // Routing Header had Segments Left == 0 (already visited, benign).
  NoSegmentsLeft,
  // Type 0 Routing Header (or unknown routing type with Segments Left > 0) was
  // rejected with ICMPv6 Parameter Problem.
  RejectedParameterProblem,
  // Malformed or mismatched Home Address in Type 2 Routing Header; drop.
  Discard,
};

// Processes a Routing Header at `routing_header_offset` in `ipv6_packet`.
// Rejects Type 0 Routing Headers with Segments Left > 0 while allowing Type 2
// Routing Headers addressed to `local_home_address`.
RoutingHeaderStatus ProcessMip6RoutingHeader(
    std::string& ipv6_packet, size_t routing_header_offset,
    const ::perception::network::IpAddress& local_home_address,
    uint32& out_problem_pointer);

// Entry in a Correspondent Node or Home Agent Binding Cache.
struct BindingCacheEntry {
  // Mobile Node's permanent Home Address.
  ::perception::network::IpAddress home_address;
  // Mobile Node's current Care-of Address.
  ::perception::network::IpAddress care_of_address;
  // Last accepted 16-bit sequence number.
  uint16 sequence_number = 0;
  // True if this entry is a Home Agent registration (H=1).
  bool is_home_registration = false;
  // Expiration timestamp.
  std::chrono::steady_clock::time_point expires_at;
};

// RFC 6275 Correspondent Node (CN) engine: handles Return Routability (HoTI/HoT
// and CoTI/CoT), authenticated Binding Updates, Binding Cache management, and
// route-optimized packet transformation.
class Mipv6CorrespondentNode {
 public:
  Mipv6CorrespondentNode(const ::perception::network::IpAddress& cn_address,
                         const std::array<uint8, 16>& secret_key);

  // Computes the 8-byte keygen token for `address` and `nonce_index`.
  std::optional<std::array<uint8, 8>> ComputeKeygenToken(
      const ::perception::network::IpAddress& address, uint16 nonce_index,
      bool is_care_of) const;

  // Rotates to a new nonce and returns its index.
  uint16 RotateNonce(const std::array<uint8, 8>& new_nonce);

  // Handles an incoming Mobility Header packet and optionally returns a reply
  // IPv6 packet (HoT, CoT, BA, or BE).
  std::optional<std::string> HandleMobilityPacket(
      const ::perception::network::IpAddress& outer_source,
      const ::perception::network::IpAddress& outer_destination,
      std::optional<::perception::network::IpAddress> home_address_option,
      std::string_view raw_mobility_header,
      std::chrono::steady_clock::time_point now);

  // Looks up a non-expired Binding Cache entry for `home_address`.
  std::optional<BindingCacheEntry> LookupBinding(
      const ::perception::network::IpAddress& home_address,
      std::chrono::steady_clock::time_point now) const;

  // Rewrites an outgoing IPv6 packet addressed to a Mobile Node's Home Address
  // to route directly to its Care-of Address with a Type 2 Routing Header.
  std::string TransformOutboundPacket(
      std::string_view ipv6_packet,
      std::chrono::steady_clock::time_point now) const;

  // Processes an incoming IPv6 packet that may carry a Home Address Destination
  // Option. If a valid binding exists, rewrites `out_normalized_packet` so its
  // source address is the Home Address. If no binding exists, populates
  // `out_binding_error_packet` with a Binding Error (status 1).
  bool ProcessInboundPacket(std::string_view ipv6_packet,
                            std::chrono::steady_clock::time_point now,
                            std::string& out_normalized_packet,
                            std::optional<std::string>& out_binding_error_packet) const;

 private:
  // Local address of this Correspondent Node.
  ::perception::network::IpAddress cn_address_;
  // Secret node key K_cn for return routability token generation.
  std::array<uint8, 16> secret_key_{};
  // Active nonces keyed by nonce index.
  std::map<uint16, std::array<uint8, 8>> nonces_;
  // Current nonce index.
  uint16 current_nonce_index_ = 1;
  // Binding Cache keyed by Mobile Node Home Address.
  std::map<::perception::network::IpAddress, BindingCacheEntry> binding_cache_;
};

// RFC 6275 Mobile Node (MN) engine: tracks Home Address and Care-of Address,
// performs Home Agent registration, Correspondent Node return routability and
// binding updates, and bidirectional tunnel encapsulation/decapsulation.
class Mipv6MobileNode {
 public:
  Mipv6MobileNode(const ::perception::network::IpAddress& home_address,
                  const ::perception::network::IpAddress& home_agent_address);

  // Returns the Mobile Node's Home Address.
  const ::perception::network::IpAddress& home_address() const {
    return home_address_;
  }

  // Returns the Mobile Node's current Care-of Address.
  const ::perception::network::IpAddress& care_of_address() const {
    return care_of_address_;
  }

  // Returns true when the Mobile Node is attached to a foreign link.
  bool IsAwayFromHome() const { return care_of_address_ != home_address_; }

  // Returns true if the Home Agent has acknowledged the current Care-of Address.
  bool is_home_registered() const { return home_registered_; }

  // Updates the Mobile Node's current Care-of Address.
  void SetCareOfAddress(const ::perception::network::IpAddress& care_of_address);

  // Builds a Home Agent Binding Update (H=1, A=1) IPv6 packet.
  std::string BuildHomeAgentBindingUpdate(uint16 lifetime_units = 450);

  // Processes a Binding Acknowledgement from the Home Agent.
  bool HandleHomeAgentBindingAck(std::string_view ipv6_packet);

  // Initiates Return Routability for `cn_address`: returns `{hoti_tunneled_pkt,
  // coti_direct_pkt}`.
  std::pair<std::string, std::string> InitiateReturnRoutability(
      const ::perception::network::IpAddress& cn_address,
      const std::array<uint8, 8>& home_cookie,
      const std::array<uint8, 8>& care_of_cookie);

  // Processes a Home Test (HoT) or Care-of Test (CoT) message from `cn_address`.
  // When both tokens have been collected, returns the signed Binding Update
  // IPv6 packet (carrying a Home Address Destination Option) to send to the CN.
  std::optional<std::string> HandleReturnRoutabilityResponse(
      const ::perception::network::IpAddress& cn_address,
      const MobilityMessage& message, uint16 lifetime_units = 105);

  // Records a confirmed Correspondent Node binding upon receiving a BA (or
  // when A=0 after sending a BU).
  void ConfirmCorrespondentBinding(
      const ::perception::network::IpAddress& cn_address);

  // Encapsulates an outgoing IPv6 packet (from `home_address` to a peer): uses
  // route optimization (Home Address Destination Option) if `cn_address` has an
  // active binding, or IPv6-in-IPv6 reverse tunneling via the Home Agent
  // otherwise.
  std::string PrepareOutboundPacket(std::string_view inner_ipv6_packet) const;

  // Decapsulates an incoming IPv6-in-IPv6 tunneled packet from the Home Agent
  // or processes a Type 2 Routing Header on a route-optimized packet.
  std::optional<std::string> ProcessInboundPacket(
      std::string_view outer_ipv6_packet) const;

 private:
  // State of an in-progress or completed Return Routability exchange with a CN.
  struct CorrespondentState {
    std::array<uint8, 8> home_cookie{};
    std::array<uint8, 8> care_of_cookie{};
    std::optional<uint16> home_nonce_index;
    std::optional<std::array<uint8, 8>> home_token;
    std::optional<uint16> care_of_nonce_index;
    std::optional<std::array<uint8, 8>> care_of_token;
    bool binding_active = false;
  };

  // Permanent Home Address.
  ::perception::network::IpAddress home_address_;
  // Home Agent unicast address.
  ::perception::network::IpAddress home_agent_address_;
  // Current Care-of Address (equals `home_address_` when at home).
  ::perception::network::IpAddress care_of_address_;
  // Next Binding Update sequence number.
  uint16 next_sequence_number_ = 1;
  // Last sequence number sent to the Home Agent.
  uint16 last_ha_sequence_number_ = 0;
  // True once the Home Agent acknowledges the current registration.
  bool home_registered_ = false;
  // Per-correspondent Return Routability and binding state.
  std::map<::perception::network::IpAddress, CorrespondentState> cn_states_;
};

// RFC 6275 Home Agent (HA) engine: maintains the Binding Cache for registered
// Mobile Nodes, tracks Proxy NDP targets on the home link, tunnels intercepted
// packets to a Mobile Node's Care-of Address, and decapsulates reverse-tunneled
// packets from Mobile Nodes.
class Mipv6HomeAgent {
 public:
  Mipv6HomeAgent(const ::perception::network::IpAddress& ha_address,
                 const ::perception::network::IpAddress& home_prefix,
                 uint8 home_prefix_length);

  // Processes an incoming Home Registration Binding Update (H=1) and returns
  // the serialized IPv6 Binding Acknowledgement packet.
  std::optional<std::string> HandleBindingUpdate(
      const ::perception::network::IpAddress& outer_source,
      std::optional<::perception::network::IpAddress> home_address_option,
      const MobilityMessage& bu, std::chrono::steady_clock::time_point now);

  // Returns true if `target_address` is a Mobile Node address (or its
  // solicited-node multicast group) for which the Home Agent proxies NDP.
  bool ShouldProxyNdpFor(
      const ::perception::network::IpAddress& target_address,
      std::chrono::steady_clock::time_point now) const;

  // Returns all currently registered proxy NDP target addresses.
  const std::set<::perception::network::IpAddress>& ProxyNdpTargets() const {
    return proxy_ndp_targets_;
  }

  // Intercepts an IPv6 packet addressed to a registered Mobile Node's Home
  // Address and encapsulates it in an IPv6-in-IPv6 tunnel to its Care-of
  // Address. Returns nullopt if the destination is not a registered Mobile Node.
  std::optional<std::string> InterceptAndTunnelPacket(
      std::string_view inner_ipv6_packet,
      std::chrono::steady_clock::time_point now) const;

  // Decapsulates a reverse-tunneled IPv6-in-IPv6 packet sent by a registered
  // Mobile Node from its Care-of Address to the Home Agent.
  std::optional<std::string> DecapsulateReverseTunnelPacket(
      std::string_view outer_ipv6_packet,
      std::chrono::steady_clock::time_point now) const;

  // Looks up a registered Mobile Node's binding.
  std::optional<BindingCacheEntry> LookupBinding(
      const ::perception::network::IpAddress& home_address,
      std::chrono::steady_clock::time_point now) const;

 private:
  // Home Agent's own global address.
  ::perception::network::IpAddress ha_address_;
  // Home subnet prefix.
  ::perception::network::IpAddress home_prefix_;
  // Home subnet prefix length.
  uint8 home_prefix_length_ = 64;
  // Registered Mobile Node bindings keyed by Home Address.
  std::map<::perception::network::IpAddress, BindingCacheEntry> bindings_;
  // Addresses on the home link for which the Home Agent defends/proxies NDP.
  std::set<::perception::network::IpAddress> proxy_ndp_targets_;
};
