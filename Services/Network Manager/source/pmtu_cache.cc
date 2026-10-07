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

#include "pmtu_cache.h"

#include <algorithm>
#include <array>

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

namespace {

// Minimum IPv6 Path MTU in bytes (RFC 8200 section 5 / RFC 8201 section 4).
constexpr uint16 kMinIpv6Pmtu = 1280;

// Minimum IPv4 Path MTU in bytes (RFC 791 / RFC 1191).
constexpr uint16 kMinIpv4Pmtu = 68;

// Lifetime of a cached PMTU entry in seconds (10 minutes, RFC 8201 section 4).
constexpr int kPmtuLifetimeSeconds = 600;

// Maximum number of destinations held in the PMTU cache.
constexpr size_t kMaxPmtuEntries = 256;

// RFC 1191 section 7.1 plateau table in descending order, used when an IPv4
// Fragmentation Needed message reports a next-hop MTU of 0.
constexpr std::array<uint16, 11> kRfc1191Plateaus = {
    65535, 32000, 17914, 8166, 4352, 2002, 1492, 1006, 576, 296, 68};

// Selects the next lower plateau below `current_mtu` from RFC 1191.
uint16 NextLowerIpv4Plateau(uint16 current_mtu) {
  for (uint16 plateau : kRfc1191Plateaus)
    if (plateau < current_mtu) return plateau;
  return kMinIpv4Pmtu;
}

}  // namespace

uint16 PmtuCache::GetPathMtu(const IpAddress& destination, uint16 link_mtu,
                             std::chrono::steady_clock::time_point now) const {
  auto it = entries_.find(destination);
  if (it == entries_.end() || it->second.expires <= now) return link_mtu;
  return std::min(it->second.mtu, link_mtu);
}

std::optional<uint16> PmtuCache::OnPacketTooBig(
    const IpAddress& destination, uint32 reported_mtu, uint16 link_mtu,
    std::chrono::steady_clock::time_point now) {
  if (destination.family() != IpAddressFamily::V4 &&
      destination.family() != IpAddressFamily::V6)
    return std::nullopt;

  uint16 current_pmtu = GetPathMtu(destination, link_mtu, now);
  uint16 new_pmtu = 0;

  if (destination.IsV6()) {
    if (reported_mtu < kMinIpv6Pmtu) return std::nullopt;
    if (reported_mtu >= current_pmtu) return std::nullopt;
    new_pmtu = static_cast<uint16>(reported_mtu);
  } else {
    if (reported_mtu == 0) {
      new_pmtu = NextLowerIpv4Plateau(current_pmtu);
    } else {
      uint32 clamped = std::max<uint32>(reported_mtu, kMinIpv4Pmtu);
      if (clamped >= current_pmtu) return std::nullopt;
      new_pmtu = static_cast<uint16>(clamped);
    }
    if (new_pmtu >= current_pmtu) return std::nullopt;
  }

  if (!entries_.contains(destination) && entries_.size() >= kMaxPmtuEntries)
    MakeRoom(now);
  entries_[destination] = {
      new_pmtu, now + std::chrono::seconds(kPmtuLifetimeSeconds)};
  return new_pmtu;
}

void PmtuCache::Purge(std::chrono::steady_clock::time_point now) {
  std::erase_if(entries_, [now](const auto& pair) {
    return pair.second.expires <= now;
  });
}

void PmtuCache::MakeRoom(std::chrono::steady_clock::time_point now) {
  Purge(now);
  if (entries_.size() < kMaxPmtuEntries) return;
  auto soonest = std::min_element(
      entries_.begin(), entries_.end(), [](const auto& a, const auto& b) {
        return a.second.expires < b.second.expires;
      });
  entries_.erase(soonest);
}
