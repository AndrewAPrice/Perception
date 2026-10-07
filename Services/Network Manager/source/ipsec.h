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
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "perception/network/ip_address.h"

// IP protocol / IPv6 Next Header number for Encapsulating Security Payload
// (RFC 4303).
inline constexpr uint8 kIpProtocolEsp = 50;

// IP protocol / IPv6 Next Header number for IPv4-in-IP encapsulation.
inline constexpr uint8 kIpProtocolIpv4Encap = 4;

// Policy action in the Security Policy Database (RFC 4301 §4.4.1).
enum class SpdAction : uint8 {
  Protect = 0,
  Bypass = 1,
  Discard = 2,
};

// IPsec encapsulation mode (RFC 4301 §3.2).
enum class IpsecMode : uint8 {
  Transport = 0,
  Tunnel = 1,
};

// AEAD cipher suites supported by ESP and IKEv2.
enum class EspCipherSuite : uint8 {
  // AES-128-GCM with 16-byte ICV and 4-byte salt (RFC 4106).
  Aes128Gcm16 = 0,
  // ChaCha20-Poly1305 with 16-byte ICV and 4-byte salt (RFC 7634).
  ChaCha20Poly1305 = 1,
};

// Traffic selector rule in the Security Policy Database (SPD).
struct SpdRule {
  // Address family filter (Unspecified matches both IPv4 and IPv6).
  ::perception::network::IpAddressFamily family =
      ::perception::network::IpAddressFamily::Unspecified;
  // Source/local prefix and prefix length (0 matches any).
  ::perception::network::IpAddress src_prefix;
  uint8 src_prefix_length = 0;
  // Destination/remote prefix and prefix length (0 matches any).
  ::perception::network::IpAddress dst_prefix;
  uint8 dst_prefix_length = 0;
  // Upper-layer protocol (0 matches any).
  uint8 protocol = 0;
  // Inclusive source port range.
  uint16 src_port_min = 0;
  uint16 src_port_max = 0xFFFF;
  // Inclusive destination port range.
  uint16 dst_port_min = 0;
  uint16 dst_port_max = 0xFFFF;
  // Policy action (Protect, Bypass, Discard).
  SpdAction action = SpdAction::Bypass;
  // IPsec mode when action is Protect.
  IpsecMode mode = IpsecMode::Transport;
  // Associated Security Parameters Index (SPI) in host byte order when
  // action is Protect.
  uint32 sa_spi = 0;
};

// Extracted 5-tuple from an IPv4 or IPv6 packet for SPD matching.
struct PacketSelectorTuple {
  ::perception::network::IpAddressFamily family =
      ::perception::network::IpAddressFamily::Unspecified;
  ::perception::network::IpAddress source;
  ::perception::network::IpAddress destination;
  uint8 protocol = 0;
  uint16 source_port = 0;
  uint16 destination_port = 0;
};

// Extracts the 5-tuple selector fields from an IPv4 or IPv6 packet.
std::optional<PacketSelectorTuple> ExtractPacketSelector(
    std::string_view ip_packet);

// 64-bit sliding anti-replay window per RFC 4303 §3.4.3.
class AntiReplayWindow {
 public:
  // Checks whether `sequence_number` is acceptable (non-zero, not too old, and
  // not already seen) without mutating the window state.
  bool Check(uint64 sequence_number) const;

  // Commits `sequence_number` into the sliding window after AEAD verification
  // succeeds.
  void Advance(uint64 sequence_number);

  // Returns the highest authenticated sequence number received so far.
  uint64 highest_sequence_number() const { return highest_seq_; }

 private:
  // Highest sequence number authenticated so far.
  uint64 highest_seq_ = 0;
  // 64-bit bitmap where bit 0 corresponds to `highest_seq_` and bit `k`
  // corresponds to `highest_seq_ - k`.
  uint64 bitmap_ = 0;
};

// An entry in the Security Association Database (SAD).
struct SecurityAssociation {
  // Security Parameters Index (host byte order).
  uint32 spi = 0;
  // Encapsulation mode (Transport or Tunnel).
  IpsecMode mode = IpsecMode::Transport;
  // AEAD cipher suite.
  EspCipherSuite cipher = EspCipherSuite::ChaCha20Poly1305;
  // Symmetric encryption key (first 16 bytes used for AES-128-GCM, all 32 bytes
  // used for ChaCha20-Poly1305).
  std::array<uint8, 32> key{};
  // 4-byte AEAD salt (combined with the 8-byte explicit IV to form the 12-byte
  // nonce).
  std::array<uint8, 4> salt{};
  // Outer source address when `mode == IpsecMode::Tunnel`.
  ::perception::network::IpAddress tunnel_local;
  // Outer destination address when `mode == IpsecMode::Tunnel`.
  ::perception::network::IpAddress tunnel_remote;
  // Next 32-bit sequence number to emit on outbound packets.
  uint32 next_tx_seq = 1;
  // Anti-replay sliding window for inbound packets.
  AntiReplayWindow replay_window;
};

// AEAD encryption helper (AES-128-GCM or ChaCha20-Poly1305) returning
// `ciphertext || 16-byte tag`.
std::string AeadEncrypt(EspCipherSuite cipher,
                        const std::array<uint8, 32>& key,
                        const std::array<uint8, 12>& nonce,
                        std::string_view aad, std::string_view plaintext);

// AEAD decryption and constant-time tag verification helper. Returns nullopt if
// the 16-byte authentication tag does not match.
std::optional<std::string> AeadDecrypt(EspCipherSuite cipher,
                                       const std::array<uint8, 32>& key,
                                       const std::array<uint8, 12>& nonce,
                                       std::string_view aad,
                                       std::string_view ciphertext_and_tag);

// Outcome of processing an outbound or inbound packet through the IPsec engine.
enum class IpsecStatus : uint8 {
  // Packet matched a Bypass policy and is returned unmodified.
  Bypassed = 0,
  // Packet was ESP-protected (outbound) or ESP-decapsulated (inbound).
  Protected = 1,
  // Packet was dropped due to a Discard policy, missing SA, anti-replay
  // rejection, or AEAD authentication failure.
  Dropped = 2,
};

// Result of an IPsec engine outbound or inbound operation.
struct IpsecResult {
  IpsecStatus status = IpsecStatus::Dropped;
  std::string packet;
  uint32 spi = 0;
};

// Combined Security Policy Database (SPD), Security Association Database (SAD),
// and RFC 4303 ESP packet protector/decapsulator for IPv4 and IPv6.
class IpsecEngine {
 public:
  // Appends an ordered selector rule to the Security Policy Database.
  void AddSpdRule(const SpdRule& rule);

  // Clears all SPD rules.
  void ClearSpd() { spd_.clear(); }

  // Installs or updates a Security Association in the SAD.
  void InstallSa(const SecurityAssociation& sa);

  // Removes a Security Association by SPI.
  bool RemoveSa(uint32 spi);

  // Looks up a Security Association by SPI.
  const SecurityAssociation* FindSa(uint32 spi) const;
  SecurityAssociation* FindSaMutable(uint32 spi);

  // Updates the outer tunnel endpoints of a Tunnel-mode SA (used by MOBIKE
  // RFC 4555 when a peer's address changes).
  bool UpdateTunnelEndpoints(
      uint32 spi, const ::perception::network::IpAddress& new_local,
      const ::perception::network::IpAddress& new_remote);

  // Evaluates the SPD for `tuple` and returns the first matching rule.
  const SpdRule* MatchSpd(const PacketSelectorTuple& tuple) const;

  // Processes an outbound IPv4 or IPv6 packet against the SPD and SAD.
  IpsecResult ProcessOutbound(std::string_view ip_packet);

  // Processes an inbound IPv4 or IPv6 packet: if the packet carries ESP
  // (protocol 50), verifies anti-replay and AEAD integrity, decapsulates
  // Transport or Tunnel mode, and returns the cleartext packet; otherwise
  // verifies that the cleartext packet is permitted by the SPD.
  IpsecResult ProcessInbound(std::string_view ip_packet);

 private:
  // Protects `ip_packet` using `sa` in Transport or Tunnel mode.
  std::optional<std::string> ProtectWithSa(std::string_view ip_packet,
                                           SecurityAssociation& sa);

  // Ordered Security Policy Database rules.
  std::vector<SpdRule> spd_;
  // Security Association Database keyed by SPI (host byte order).
  std::map<uint32, SecurityAssociation> sad_;
};
