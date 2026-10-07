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

#include "routing_table.h"

#include <algorithm>
#include <array>

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

namespace {

// Administrative distance for directly connected routes.
constexpr uint8 kAdminDistanceConnected = 0;

// Administrative distance for static routes.
constexpr uint8 kAdminDistanceStatic = 1;

// Administrative distance for Router Advertisement routes.
constexpr uint8 kAdminDistanceRouterAdvertisement = 100;

// Maximum prefix length in bits for IPv4.
constexpr uint8 kMaxIpv4PrefixLength = 32;

// Maximum prefix length in bits for IPv6.
constexpr uint8 kMaxIpv6PrefixLength = 128;

// Clamps a prefix length to the address family's bit width.
uint8 ClampPrefixLength(IpAddressFamily family, uint8 prefix_length) {
  if (family == IpAddressFamily::V4)
    return std::min(prefix_length, kMaxIpv4PrefixLength);
  if (family == IpAddressFamily::V6)
    return std::min(prefix_length, kMaxIpv6PrefixLength);
  return 0;
}

// Returns true if `candidate` is strictly preferred over `best`.
bool IsBetterRoute(const RouteEntry& candidate, const RouteEntry& best) {
  if (candidate.prefix_length != best.prefix_length)
    return candidate.prefix_length > best.prefix_length;
  if (candidate.admin_distance != best.admin_distance)
    return candidate.admin_distance < best.admin_distance;
  return candidate.metric < best.metric;
}

// Returns true if `entry` has expired at `now`.
bool IsExpired(const RouteEntry& entry,
               std::optional<std::chrono::steady_clock::time_point> now) {
  if (!now.has_value() || !entry.expires_at.has_value()) return false;
  return *entry.expires_at <= *now;
}

}  // namespace

uint8 DefaultAdminDistance(RouteOrigin origin) {
  switch (origin) {
    case RouteOrigin::Connected:
      return kAdminDistanceConnected;
    case RouteOrigin::Static:
      return kAdminDistanceStatic;
    case RouteOrigin::RouterAdvertisement:
      return kAdminDistanceRouterAdvertisement;
  }
  return kAdminDistanceStatic;
}

IpAddress MaskPrefix(const IpAddress& address, uint8 prefix_length) {
  if (address.family() == IpAddressFamily::Unspecified) return IpAddress();
  uint8 clamped = ClampPrefixLength(address.family(), prefix_length);
  std::array<uint8, IpAddress::kV6Length> masked = address.bytes();
  size_t total_bytes = address.Length();
  size_t full_bytes = clamped / 8;
  uint8 remaining_bits = clamped % 8;

  if (full_bytes < total_bytes && remaining_bits > 0) {
    uint8 mask = static_cast<uint8>(0xFF << (8 - remaining_bits));
    masked[full_bytes] &= mask;
    full_bytes++;
  }
  for (size_t i = full_bytes; i < masked.size(); i++) masked[i] = 0;

  if (address.IsV4()) {
    return IpAddress::V4({masked[0], masked[1], masked[2], masked[3]});
  }
  return IpAddress::V6(masked);
}

void RoutingTable::AddRoute(const RouteEntry& entry) {
  if (entry.prefix.family() == IpAddressFamily::Unspecified) return;
  RouteEntry normalized = entry;
  normalized.prefix_length =
      ClampPrefixLength(entry.prefix.family(), entry.prefix_length);
  normalized.prefix = MaskPrefix(entry.prefix, normalized.prefix_length);

  for (RouteEntry& existing : routes_) {
    if (existing.prefix == normalized.prefix &&
        existing.prefix_length == normalized.prefix_length &&
        existing.origin == normalized.origin &&
        existing.next_hop == normalized.next_hop &&
        existing.interface_index == normalized.interface_index) {
      existing.admin_distance = normalized.admin_distance;
      existing.metric = normalized.metric;
      existing.expires_at = normalized.expires_at;
      return;
    }
  }
  routes_.push_back(normalized);
}

void RoutingTable::AddConnectedRoute(const IpAddress& prefix,
                                     uint8 prefix_length,
                                     size_t interface_index, uint32 metric) {
  RouteEntry entry;
  entry.prefix = prefix;
  entry.prefix_length = prefix_length;
  entry.next_hop = IpAddress();
  entry.interface_index = interface_index;
  entry.origin = RouteOrigin::Connected;
  entry.admin_distance = DefaultAdminDistance(RouteOrigin::Connected);
  entry.metric = metric;
  AddRoute(entry);
}

void RoutingTable::AddStaticRoute(const IpAddress& prefix, uint8 prefix_length,
                                  const IpAddress& next_hop,
                                  size_t interface_index, uint32 metric,
                                  uint8 admin_distance) {
  RouteEntry entry;
  entry.prefix = prefix;
  entry.prefix_length = prefix_length;
  entry.next_hop = next_hop;
  entry.interface_index = interface_index;
  entry.origin = RouteOrigin::Static;
  entry.admin_distance = admin_distance;
  entry.metric = metric;
  AddRoute(entry);
}

void RoutingTable::AddRaRoute(
    const IpAddress& prefix, uint8 prefix_length, const IpAddress& next_hop,
    size_t interface_index, std::chrono::steady_clock::time_point expires_at,
    uint32 metric) {
  RouteEntry entry;
  entry.prefix = prefix;
  entry.prefix_length = prefix_length;
  entry.next_hop = next_hop;
  entry.interface_index = interface_index;
  entry.origin = RouteOrigin::RouterAdvertisement;
  entry.admin_distance = DefaultAdminDistance(RouteOrigin::RouterAdvertisement);
  entry.metric = metric;
  entry.expires_at = expires_at;
  AddRoute(entry);
}

bool RoutingTable::RemoveRoute(const IpAddress& prefix, uint8 prefix_length,
                               std::optional<RouteOrigin> origin,
                               std::optional<IpAddress> next_hop,
                               std::optional<size_t> interface_index) {
  uint8 clamped = ClampPrefixLength(prefix.family(), prefix_length);
  IpAddress masked = MaskPrefix(prefix, clamped);
  size_t before = routes_.size();
  std::erase_if(routes_, [&](const RouteEntry& entry) {
    if (entry.prefix != masked || entry.prefix_length != clamped) return false;
    if (origin.has_value() && entry.origin != *origin) return false;
    if (next_hop.has_value() && entry.next_hop != *next_hop) return false;
    if (interface_index.has_value() &&
        entry.interface_index != *interface_index)
      return false;
    return true;
  });
  return routes_.size() != before;
}

void RoutingTable::RemoveRoutesForInterface(size_t interface_index) {
  std::erase_if(routes_, [&](const RouteEntry& entry) {
    return entry.interface_index == interface_index;
  });
}

void RoutingTable::PurgeExpired(std::chrono::steady_clock::time_point now) {
  std::erase_if(routes_, [&](const RouteEntry& entry) {
    return IsExpired(entry, now);
  });
}

std::optional<ResolvedRoute> RoutingTable::Lookup(
    const IpAddress& destination,
    std::optional<std::chrono::steady_clock::time_point> now) const {
  if (destination.family() == IpAddressFamily::Unspecified) return std::nullopt;

  const RouteEntry* best = nullptr;
  for (const RouteEntry& entry : routes_) {
    if (entry.prefix.family() != destination.family()) continue;
    if (IsExpired(entry, now)) continue;
    if (!destination.IsInPrefix(entry.prefix, entry.prefix_length)) continue;
    if (best == nullptr || IsBetterRoute(entry, *best)) best = &entry;
  }

  if (best == nullptr) return std::nullopt;

  ResolvedRoute resolved;
  resolved.route = *best;
  resolved.interface_index = best->interface_index;
  resolved.immediate_next_hop =
      best->IsOnLink() ? destination : best->next_hop;
  return resolved;
}
