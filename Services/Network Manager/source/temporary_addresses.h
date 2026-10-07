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
#include <functional>
#include <optional>
#include <vector>

#include "interface_address.h"
#include "ndp.h"
#include "perception/network/ip_address.h"

// Default system-wide policy knob for RFC 8981 temporary SLAAC addresses.
// Disabled by default so primary SLAAC addresses remain deterministic unless
// privacy extensions are explicitly enabled on an interface.
inline constexpr bool kEnableTemporaryAddresses = false;

// Default maximum valid lifetime for a temporary address (2 days, RFC 8981 §3.8).
inline constexpr uint32 kTempValidLifetimeSeconds = 172800;

// Default maximum preferred lifetime for a temporary address (1 day, RFC 8981 §3.8).
inline constexpr uint32 kTempPreferredLifetimeSeconds = 86400;

// Advance time before deprecation at which a replacement temporary address is
// generated (5 seconds, RFC 8981 §3.8).
inline constexpr uint32 kTempRegenAdvanceSeconds = 5;

// Maximum number of DAD regeneration attempts after an IID collision (RFC 8981 §3.8).
inline constexpr uint8 kTempIdgenRetries = 3;

// Callback returning 64 bits of random or keyed-hash material for temporary IIDs.
using TemporaryIidRandomFn = std::function<uint64()>;

// Returns true if `iid` is a reserved IPv6 interface identifier that must not
// be assigned to a temporary address (RFC 8981 §3.3.2, RFC 5453).
bool IsReservedTemporaryInterfaceIdentifier(const std::array<uint8, 8>& iid);

// Generates a 64-bit temporary interface identifier using `random_fn`, avoiding
// reserved identifiers and `stable_iid`.
std::array<uint8, 8> GenerateTemporaryInterfaceIdentifier(
    const std::array<uint8, 8>& stable_iid,
    const TemporaryIidRandomFn& random_fn);

// Manages RFC 8981 temporary privacy addresses alongside stable SLAAC prefixes.
class TemporaryAddressManager {
 public:
  // Callback used to transmit a DAD Neighbor Solicitation for `target`.
  using DadProbeFn =
      std::function<void(const ::perception::network::IpAddress& target)>;

  // Callback invoked when the manager joins or leaves a solicited-node
  // multicast group.
  using MulticastChangeFn = std::function<void(
      const ::perception::network::IpAddress& group, bool joined)>;

  TemporaryAddressManager(const HardwareAddress& mac, DadProbeFn dad_probe_fn,
                          MulticastChangeFn multicast_fn = {},
                          TemporaryIidRandomFn random_fn = {},
                          bool enabled = kEnableTemporaryAddresses);

  // Enables or disables temporary address generation.
  void SetEnabled(bool enabled) { enabled_ = enabled; }

  // Returns true if temporary address generation is enabled.
  bool enabled() const { return enabled_; }

  // Processes an on-link autonomous /64 Prefix Information Option from a Router
  // Advertisement at `now`.
  void OnPrefixInformation(const NdpPrefixInformation& pio,
                           std::chrono::steady_clock::time_point now);

  // Processes an incoming Neighbor Solicitation for DAD collision detection.
  void OnNeighborSolicitation(const ::perception::network::IpAddress& ip_source,
                              const NdpNeighborSolicitation& ns,
                              std::chrono::steady_clock::time_point now);

  // Processes an incoming Neighbor Advertisement for DAD collision detection.
  void OnNeighborAdvertisement(const NdpNeighborAdvertisement& na,
                               std::chrono::steady_clock::time_point now);

  // Advances DAD completion, regeneration, deprecation, and expiration timers
  // at `now`.
  void OnTimer(std::chrono::steady_clock::time_point now);

  // Returns all active temporary addresses (Tentative, Preferred, Deprecated).
  const std::vector<InterfaceAddress>& addresses() const { return addresses_; }

  // Returns the most recently preferred temporary address matching `prefix`, or
  // nullopt if none is preferred.
  std::optional<::perception::network::IpAddress> PreferredTemporaryAddress(
      const ::perception::network::IpAddress& prefix,
      uint8 prefix_length = 64) const;

  // Returns the earliest timer deadline across all tracked temporary addresses.
  std::optional<std::chrono::steady_clock::time_point> NextDeadline() const;

 private:
  // Tracked state for an autonomous /64 prefix.
  struct TrackedPrefix {
    // 64-bit prefix network address.
    ::perception::network::IpAddress prefix;
    // Latest valid lifetime advertised by the router (capped to TEMP_VALID).
    uint32 valid_lifetime_seconds = 0;
    // Latest preferred lifetime advertised by the router (capped to TEMP_PREFERRED).
    uint32 preferred_lifetime_seconds = 0;
    // Remaining DAD regeneration retries on this prefix.
    uint8 dad_retries_remaining = kTempIdgenRetries;
  };

  // Spawns a new tentative temporary address for `tracked` at `now`.
  void SpawnTemporaryAddress(TrackedPrefix& tracked,
                             std::chrono::steady_clock::time_point now);

  // Handles a DAD collision on `address` at `now`.
  void HandleDadCollision(const ::perception::network::IpAddress& address,
                          std::chrono::steady_clock::time_point now);

  // Returns true if `tracked` currently has an non-deprecated (Tentative or
  // Preferred) temporary address whose preferred lifetime extends beyond the
  // regeneration window.
  bool HasFreshAddressForPrefix(
      const TrackedPrefix& tracked,
      std::chrono::steady_clock::time_point now) const;

  // Stable Modified EUI-64 IID to avoid colliding with the interface's SLAAC address.
  std::array<uint8, 8> stable_iid_{};
  // DAD probe sender.
  DadProbeFn dad_probe_fn_;
  // Solicited-node multicast group notifier.
  MulticastChangeFn multicast_fn_;
  // Random 64-bit generator.
  TemporaryIidRandomFn random_fn_;
  // Fallback SplitMix64 state when no external generator is supplied.
  uint64 prng_state_ = 0;
  // Whether RFC 8981 temporary address generation is enabled.
  bool enabled_ = kEnableTemporaryAddresses;
  // Active temporary addresses.
  std::vector<InterfaceAddress> addresses_;
  // DAD completion deadlines for Tentative addresses.
  std::map<::perception::network::IpAddress,
           std::chrono::steady_clock::time_point>
      dad_complete_at_;
  // Tracked /64 prefixes.
  std::vector<TrackedPrefix> prefixes_;
};
