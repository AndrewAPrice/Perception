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
#include <optional>
#include <string>
#include <string_view>

#include "ipv6_header.h"
#include "perception/network/ip_address.h"

// ICMPv6 message types (RFC 4443, RFC 4861, RFC 3810).
enum class Icmpv6Type : uint8 {
  DestinationUnreachable = 1,
  PacketTooBig = 2,
  TimeExceeded = 3,
  ParameterProblem = 4,
  EchoRequest = 128,
  EchoReply = 129,
  MulticastListenerQuery = 130,
  MulticastListenerReportV1 = 131,
  MulticastListenerDone = 132,
  RouterSolicitation = 133,
  RouterAdvertisement = 134,
  NeighborSolicitation = 135,
  NeighborAdvertisement = 136,
  Redirect = 137,
  MulticastListenerReportV2 = 143,
};

// Destination Unreachable codes (RFC 4443 §3.1).
enum class Icmpv6UnreachableCode : uint8 {
  NoRouteToDestination = 0,
  AdministrativelyProhibited = 1,
  BeyondScopeOfSourceAddress = 2,
  AddressUnreachable = 3,
  PortUnreachable = 4,
};

// Time Exceeded codes (RFC 4443 §3.3).
enum class Icmpv6TimeExceededCode : uint8 {
  HopLimitExceeded = 0,
  FragmentReassemblyTimeExceeded = 1,
};

// Parameter Problem codes (RFC 4443 §3.4).
enum class Icmpv6ParameterProblemCode : uint8 {
  ErroneousHeaderField = 0,
  UnrecognizedNextHeader = 1,
  UnrecognizedIpv6Option = 2,
};

// Returns true if `type` is an ICMPv6 error message (0..127).
bool IsIcmpv6ErrorMessage(uint8 type);

// Verifies the ICMPv6 pseudo-header checksum of `icmpv6_packet`.
bool VerifyIcmpv6Checksum(const ::perception::network::IpAddress& source,
                          const ::perception::network::IpAddress& destination,
                          std::string_view icmpv6_packet);

// Computes and writes the 16-bit checksum into bytes 2..3 of `icmpv6_packet`.
void FinalizeIcmpv6Checksum(const ::perception::network::IpAddress& source,
                            const ::perception::network::IpAddress& destination,
                            std::string& icmpv6_packet);

// Parsed ICMPv6 Echo Request or Echo Reply message.
struct Icmpv6EchoMessage {
  // True for Echo Reply (129), false for Echo Request (128).
  bool is_reply = false;
  // Echo identifier.
  uint16 identifier = 0;
  // Echo sequence number.
  uint16 sequence = 0;
  // Payload bytes following the 8-byte echo header.
  std::string data;
};

// Parses an ICMPv6 Echo Request or Echo Reply (checksum must already be
// verified by the caller).
std::optional<Icmpv6EchoMessage> ParseIcmpv6Echo(
    std::string_view icmpv6_packet);

// Serializes an ICMPv6 Echo Request or Echo Reply and fills in its checksum.
std::string BuildIcmpv6Echo(
    const ::perception::network::IpAddress& source,
    const ::perception::network::IpAddress& destination,
    const Icmpv6EchoMessage& message);

// Builds an outgoing IPv6 datagram carrying an Echo Reply for `request`.
Ipv6Datagram MakeIcmpv6EchoReply(
    const ::perception::network::IpAddress& local_source,
    const ::perception::network::IpAddress& remote_destination,
    uint8 hop_limit, const Icmpv6EchoMessage& request);

// Parsed ICMPv6 error message (Destination Unreachable, Packet Too Big, Time
// Exceeded, or Parameter Problem).
struct Icmpv6ErrorMessage {
  // Error message type (1..4).
  Icmpv6Type type = Icmpv6Type::DestinationUnreachable;
  // Error subtype code.
  uint8 code = 0;
  // 32-bit field following the checksum: MTU for PacketTooBig, byte offset
  // pointer for ParameterProblem, or unused (0) for other errors.
  uint32 parameter = 0;
  // Quoted slice of the invoking IPv6 packet.
  std::string_view invoking_packet;
  // Parsed fixed IPv6 header of `invoking_packet` when at least 40 bytes are
  // present and valid.
  std::optional<Ipv6Header> invoking_header;
};

// Parses an ICMPv6 error message (types 1..4).
std::optional<Icmpv6ErrorMessage> ParseIcmpv6Error(
    std::string_view icmpv6_packet);

// Returns true if RFC 4443 §2.4(e) permits originating an ICMPv6 error message
// of `error_type` and `error_code` in response to `invoking_packet`.
bool CanSendIcmpv6Error(Icmpv6Type error_type, uint8 error_code,
                        std::string_view invoking_packet);

// Builds an ICMPv6 error message (with checksum) quoting as much of
// `invoking_packet` as fits within the minimum IPv6 MTU (1280 bytes). Returns
// nullopt if RFC 4443 §2.4(e) forbids sending the error.
std::optional<std::string> BuildIcmpv6Error(
    const ::perception::network::IpAddress& source,
    const ::perception::network::IpAddress& destination, Icmpv6Type type,
    uint8 code, uint32 parameter, std::string_view invoking_packet);

// Token-bucket rate limiter for outgoing ICMPv6 error messages (RFC 4443 §2.4).
// Default rate is 10 messages per second with a burst of 10.
class Icmpv6RateLimiter {
 public:
  // Default refill rate in tokens per second.
  static constexpr double kDefaultRatePerSecond = 10.0;

  // Default maximum burst size in tokens.
  static constexpr double kDefaultBurstTokens = 10.0;

  explicit Icmpv6RateLimiter(double rate_per_second = kDefaultRatePerSecond,
                             double burst_tokens = kDefaultBurstTokens);

  // Consumes one token at `now` and returns true if an error message may be
  // sent, or returns false if rate-limited.
  bool Allow(std::chrono::steady_clock::time_point now);

 private:
  // Tokens added per second.
  double rate_per_second_;
  // Maximum token capacity.
  double burst_tokens_;
  // Currently available tokens.
  double tokens_;
  // Timestamp of the previous refill calculation.
  std::optional<std::chrono::steady_clock::time_point> last_refill_;
};
