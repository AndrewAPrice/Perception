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

#include "temporary_addresses.h"

#include <algorithm>

#include "ipv6_header.h"
#include "slaac.h"

namespace {

using ::perception::network::IpAddress;

// Required prefix length for SLAAC temporary address generation (64 bits).
constexpr uint8 kTemporaryPrefixLength = 64;

// DAD wait duration before a tentative temporary address becomes Preferred.
constexpr auto kTemporaryDadWaitDuration = std::chrono::milliseconds(1000);

// Upper limit on internal loop iterations when rejecting reserved IIDs.
constexpr size_t kMaxIidDrawAttempts = 16;

// High 56 bits of the RFC 2526 reserved subnet anycast range
// (fdff:ffff:ffff:ff80..fdff:ffff:ffff:ffff).
constexpr std::array<uint8, 7> kSubnetAnycastPrefix = {0xfd, 0xff, 0xff, 0xff,
                                                       0xff, 0xff, 0xff};

// Lowest final byte of the RFC 2526 reserved subnet anycast range.
constexpr uint8 kSubnetAnycastMinLowByte = 0x80;

// Prefix bytes of the IANA Ethernet block (0200:5eff:fe00:0000..0200:5eff:feff:ffff).
constexpr std::array<uint8, 5> kIanaEthernetIidPrefix = {0x02, 0x00, 0x5e,
                                                         0xff, 0xfe};

// SplitMix64 mixing constant 1.
constexpr uint64 kSplitMixIncrement = 0x9e3779b97f4a7c15ULL;

// SplitMix64 multiplier 1.
constexpr uint64 kSplitMixMul1 = 0xbf58476d1ce4e5b9ULL;

// SplitMix64 multiplier 2.
constexpr uint64 kSplitMixMul2 = 0x94d049bb133111ebULL;

// Advances `state` with SplitMix64 and returns a 64-bit pseudo-random integer.
uint64 NextSplitMix64(uint64& state) {
  uint64 z = (state += kSplitMixIncrement);
  z = (z ^ (z >> 30)) * kSplitMixMul1;
  z = (z ^ (z >> 27)) * kSplitMixMul2;
  return z ^ (z >> 31);
}

// Combines the top 64 bits of `prefix` with `iid` into an IPv6 address.
IpAddress CombinePrefixAndTemporaryIid(const IpAddress& prefix,
                                       const std::array<uint8, 8>& iid) {
  std::array<uint8, IpAddress::kV6Length> raw = prefix.bytes();
  for (size_t i = 0; i < 8; ++i) raw[8 + i] = iid[i];
  return IpAddress::V6(raw);
}

// Normalizes `prefix` so its lower 64 bits are zeroed.
IpAddress NormalizePrefix64(const IpAddress& prefix) {
  std::array<uint8, IpAddress::kV6Length> raw = prefix.bytes();
  for (size_t i = 8; i < IpAddress::kV6Length; ++i) raw[i] = 0;
  return IpAddress::V6(raw);
}

}  // namespace

bool IsReservedTemporaryInterfaceIdentifier(const std::array<uint8, 8>& iid) {
  bool all_zero = true;
  for (uint8 b : iid) {
    if (b != 0) {
      all_zero = false;
      break;
    }
  }
  if (all_zero) return true;

  bool matches_anycast_prefix = true;
  for (size_t i = 0; i < kSubnetAnycastPrefix.size(); ++i) {
    if (iid[i] != kSubnetAnycastPrefix[i]) {
      matches_anycast_prefix = false;
      break;
    }
  }
  if (matches_anycast_prefix && iid[7] >= kSubnetAnycastMinLowByte) return true;

  bool matches_iana_prefix = true;
  for (size_t i = 0; i < kIanaEthernetIidPrefix.size(); ++i) {
    if (iid[i] != kIanaEthernetIidPrefix[i]) {
      matches_iana_prefix = false;
      break;
    }
  }
  if (matches_iana_prefix) return true;

  return false;
}

std::array<uint8, 8> GenerateTemporaryInterfaceIdentifier(
    const std::array<uint8, 8>& stable_iid,
    const TemporaryIidRandomFn& random_fn) {
  for (size_t attempt = 0; attempt < kMaxIidDrawAttempts; ++attempt) {
    uint64 value = random_fn ? random_fn() : 0;
    std::array<uint8, 8> iid{};
    for (size_t i = 0; i < 8; ++i) {
      iid[i] = static_cast<uint8>((value >> ((7 - i) * 8)) & 0xffu);
    }
    if (!IsReservedTemporaryInterfaceIdentifier(iid) && iid != stable_iid)
      return iid;
  }
  std::array<uint8, 8> fallback = stable_iid;
  fallback[7] ^= 0x01;
  if (IsReservedTemporaryInterfaceIdentifier(fallback)) fallback[6] ^= 0x01;
  return fallback;
}

TemporaryAddressManager::TemporaryAddressManager(
    const HardwareAddress& mac, DadProbeFn dad_probe_fn,
    MulticastChangeFn multicast_fn, TemporaryIidRandomFn random_fn,
    bool enabled)
    : stable_iid_(ModifiedEui64InterfaceIdentifier(mac)),
      dad_probe_fn_(std::move(dad_probe_fn)),
      multicast_fn_(std::move(multicast_fn)),
      random_fn_(std::move(random_fn)),
      enabled_(enabled) {
  for (uint8 b : mac) prng_state_ = (prng_state_ << 8) | b;
  if (!random_fn_)
    random_fn_ = [this]() { return NextSplitMix64(prng_state_); };
}

void TemporaryAddressManager::OnPrefixInformation(
    const NdpPrefixInformation& pio,
    std::chrono::steady_clock::time_point now) {
  if (!enabled_) return;
  if (!pio.autonomous || pio.prefix_length != kTemporaryPrefixLength) return;
  if (!pio.prefix.IsV6() || pio.prefix.IsLinkLocal() ||
      pio.prefix.IsMulticast())
    return;
  if (pio.preferred_lifetime_seconds > pio.valid_lifetime_seconds) return;

  const IpAddress normalized_prefix = NormalizePrefix64(pio.prefix);
  const uint32 capped_valid =
      std::min(pio.valid_lifetime_seconds, kTempValidLifetimeSeconds);
  const uint32 capped_preferred =
      std::min(pio.preferred_lifetime_seconds, kTempPreferredLifetimeSeconds);

  TrackedPrefix* tracked = nullptr;
  for (auto& existing : prefixes_) {
    if (existing.prefix == normalized_prefix) {
      tracked = &existing;
      break;
    }
  }
  if (tracked == nullptr) {
    if (capped_preferred == 0 || capped_valid == 0) return;
    prefixes_.push_back({normalized_prefix, capped_valid, capped_preferred,
                         kTempIdgenRetries});
    tracked = &prefixes_.back();
  } else {
    tracked->valid_lifetime_seconds = capped_valid;
    tracked->preferred_lifetime_seconds = capped_preferred;
  }

  for (auto& entry : addresses_) {
    if (!entry.address.IsInPrefix(normalized_prefix, kTemporaryPrefixLength))
      continue;
    const auto new_valid = now + std::chrono::seconds(capped_valid);
    if (new_valid < entry.valid_until) entry.valid_until = new_valid;
    const auto new_pref = now + std::chrono::seconds(capped_preferred);
    if (new_pref < entry.preferred_until) entry.preferred_until = new_pref;
    if (capped_preferred == 0 && entry.state == AddressState::Preferred)
      entry.state = AddressState::Deprecated;
  }

  if (capped_preferred > 0 && !HasFreshAddressForPrefix(*tracked, now)) {
    tracked->dad_retries_remaining = kTempIdgenRetries;
    SpawnTemporaryAddress(*tracked, now);
  }
}

void TemporaryAddressManager::OnNeighborSolicitation(
    const IpAddress& ip_source, const NdpNeighborSolicitation& ns,
    std::chrono::steady_clock::time_point now) {
  if (ip_source.IsUnspecified()) HandleDadCollision(ns.target, now);
}

void TemporaryAddressManager::OnNeighborAdvertisement(
    const NdpNeighborAdvertisement& na,
    std::chrono::steady_clock::time_point now) {
  HandleDadCollision(na.target, now);
}

void TemporaryAddressManager::OnTimer(
    std::chrono::steady_clock::time_point now) {
  for (size_t i = 0; i < addresses_.size();) {
    auto& entry = addresses_[i];
    if (now >= entry.valid_until) {
      const IpAddress removed_addr = entry.address;
      dad_complete_at_.erase(removed_addr);
      addresses_.erase(addresses_.begin() + static_cast<ptrdiff_t>(i));
      const IpAddress group = SolicitedNodeMulticastAddress(removed_addr);
      bool still_needed = false;
      for (const auto& other : addresses_) {
        if (SolicitedNodeMulticastAddress(other.address) == group) {
          still_needed = true;
          break;
        }
      }
      if (!still_needed && multicast_fn_) multicast_fn_(group, false);
      continue;
    }

    if (entry.state == AddressState::Tentative) {
      auto dad_it = dad_complete_at_.find(entry.address);
      if (dad_it != dad_complete_at_.end() && now >= dad_it->second) {
        dad_complete_at_.erase(dad_it);
        entry.state =
            (now < entry.preferred_until) ? AddressState::Preferred
                                          : AddressState::Deprecated;
      }
    }
    if (entry.state == AddressState::Preferred &&
        now >= entry.preferred_until)
      entry.state = AddressState::Deprecated;
    ++i;
  }

  if (!enabled_) return;

  for (auto& tracked : prefixes_) {
    if (tracked.preferred_lifetime_seconds <= kTempRegenAdvanceSeconds)
      continue;
    if (!HasFreshAddressForPrefix(tracked, now)) {
      tracked.dad_retries_remaining = kTempIdgenRetries;
      SpawnTemporaryAddress(tracked, now);
    }
  }
}

std::optional<IpAddress> TemporaryAddressManager::PreferredTemporaryAddress(
    const IpAddress& prefix, uint8 prefix_length) const {
  std::optional<IpAddress> best;
  std::chrono::steady_clock::time_point latest_preferred{};
  for (const auto& entry : addresses_) {
    if (entry.state != AddressState::Preferred) continue;
    if (!entry.address.IsInPrefix(prefix, prefix_length)) continue;
    if (!best.has_value() || entry.preferred_until >= latest_preferred) {
      best = entry.address;
      latest_preferred = entry.preferred_until;
    }
  }
  return best;
}

std::optional<std::chrono::steady_clock::time_point>
TemporaryAddressManager::NextDeadline() const {
  std::optional<std::chrono::steady_clock::time_point> earliest;
  auto consider = [&](std::chrono::steady_clock::time_point t) {
    if (!earliest.has_value() || t < *earliest) earliest = t;
  };
  for (const auto& entry : addresses_) {
    consider(entry.valid_until);
    if (entry.state == AddressState::Tentative) {
      auto dad_it = dad_complete_at_.find(entry.address);
      if (dad_it != dad_complete_at_.end()) consider(dad_it->second);
    }
    if (entry.state == AddressState::Preferred) {
      consider(entry.preferred_until);
      const auto regen_at =
          entry.preferred_until - std::chrono::seconds(kTempRegenAdvanceSeconds);
      consider(regen_at);
    }
  }
  return earliest;
}

void TemporaryAddressManager::SpawnTemporaryAddress(
    TrackedPrefix& tracked, std::chrono::steady_clock::time_point now) {
  IpAddress candidate;
  for (size_t attempt = 0; attempt < kMaxIidDrawAttempts; ++attempt) {
    const auto iid =
        GenerateTemporaryInterfaceIdentifier(stable_iid_, random_fn_);
    candidate = CombinePrefixAndTemporaryIid(tracked.prefix, iid);
    bool exists = false;
    for (const auto& existing : addresses_) {
      if (existing.address == candidate) {
        exists = true;
        break;
      }
    }
    if (!exists) break;
  }

  InterfaceAddress entry;
  entry.address = candidate;
  entry.prefix_length = kTemporaryPrefixLength;
  entry.state = AddressState::Tentative;
  entry.origin = AddressOrigin::Temporary;
  entry.preferred_until =
      now + std::chrono::seconds(tracked.preferred_lifetime_seconds);
  entry.valid_until =
      now + std::chrono::seconds(tracked.valid_lifetime_seconds);
  dad_complete_at_[candidate] = now + kTemporaryDadWaitDuration;

  const IpAddress group = SolicitedNodeMulticastAddress(candidate);
  bool already_joined = false;
  for (const auto& other : addresses_) {
    if (SolicitedNodeMulticastAddress(other.address) == group) {
      already_joined = true;
      break;
    }
  }
  addresses_.push_back(entry);
  if (!already_joined && multicast_fn_) multicast_fn_(group, true);
  if (dad_probe_fn_) dad_probe_fn_(candidate);
}

void TemporaryAddressManager::HandleDadCollision(
    const IpAddress& address, std::chrono::steady_clock::time_point now) {
  for (size_t i = 0; i < addresses_.size(); ++i) {
    if (addresses_[i].address != address ||
        addresses_[i].state != AddressState::Tentative)
      continue;
    dad_complete_at_.erase(address);
    addresses_.erase(addresses_.begin() + static_cast<ptrdiff_t>(i));
    const IpAddress group = SolicitedNodeMulticastAddress(address);
    bool still_needed = false;
    for (const auto& other : addresses_) {
      if (SolicitedNodeMulticastAddress(other.address) == group) {
        still_needed = true;
        break;
      }
    }
    if (!still_needed && multicast_fn_) multicast_fn_(group, false);

    for (auto& tracked : prefixes_) {
      if (address.IsInPrefix(tracked.prefix, kTemporaryPrefixLength)) {
        if (tracked.dad_retries_remaining > 0) {
          --tracked.dad_retries_remaining;
          SpawnTemporaryAddress(tracked, now);
        }
        break;
      }
    }
    return;
  }
}

bool TemporaryAddressManager::HasFreshAddressForPrefix(
    const TrackedPrefix& tracked,
    std::chrono::steady_clock::time_point now) const {
  const auto regen_horizon =
      now + std::chrono::seconds(kTempRegenAdvanceSeconds);
  for (const auto& entry : addresses_) {
    if (!entry.address.IsInPrefix(tracked.prefix, kTemporaryPrefixLength))
      continue;
    if ((entry.state == AddressState::Tentative ||
         entry.state == AddressState::Preferred) &&
        entry.preferred_until > regen_horizon) {
      return true;
    }
  }
  return false;
}
