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
#include <vector>

#include "perception/network/ip_address.h"

// How a routing table entry was learned.
enum class RouteOrigin : uint8 {
  Connected = 0,
  Static = 1,
  RouterAdvertisement = 2,
};

// Default administrative distances for each route origin.
uint8 DefaultAdminDistance(RouteOrigin origin);

// Returns a copy of `address` with all bits after `prefix_length` cleared.
::perception::network::IpAddress MaskPrefix(
    const ::perception::network::IpAddress& address, uint8 prefix_length);

// A single entry in the dual-stack routing table.
struct RouteEntry {
  // Network prefix address (bits beyond `prefix_length` are masked to zero).
  ::perception::network::IpAddress prefix;
  // Prefix length in bits (0..32 for IPv4, 0..128 for IPv6).
  uint8 prefix_length = 0;
  // Next-hop router address; Unspecified means the prefix is directly on-link.
  ::perception::network::IpAddress next_hop;
  // Outgoing interface index.
  size_t interface_index = 0;
  // Source of the route.
  RouteOrigin origin = RouteOrigin::Static;
  // Administrative distance (lower is preferred when prefix lengths tie).
  uint8 admin_distance = 1;
  // Route metric (lower is preferred when prefix length and distance tie).
  uint32 metric = 0;
  // Optional expiration timestamp (used for RA-learned routes).
  std::optional<std::chrono::steady_clock::time_point> expires_at;

  // Returns true if the route points directly to the attached link.
  bool IsOnLink() const { return next_hop.IsUnspecified(); }
};

// Result of a longest-prefix-match lookup in the routing table.
struct ResolvedRoute {
  // The matched route entry.
  RouteEntry route;
  // Next-hop IP address to resolve at L2 (equals destination if on-link).
  ::perception::network::IpAddress immediate_next_hop;
  // Outgoing interface index.
  size_t interface_index = 0;
};

// Dual-stack longest-prefix-match routing table supporting connected routes,
// static routes, and Router Advertisement learned routes.
class RoutingTable {
 public:
  // Adds or updates a route entry.
  void AddRoute(const RouteEntry& entry);

  // Adds a directly connected on-link subnet route.
  void AddConnectedRoute(const ::perception::network::IpAddress& prefix,
                         uint8 prefix_length, size_t interface_index,
                         uint32 metric = 0);

  // Adds a configured static route.
  void AddStaticRoute(const ::perception::network::IpAddress& prefix,
                      uint8 prefix_length,
                      const ::perception::network::IpAddress& next_hop,
                      size_t interface_index, uint32 metric = 0,
                      uint8 admin_distance = 1);

  // Adds a route learned from an IPv6 Router Advertisement.
  void AddRaRoute(const ::perception::network::IpAddress& prefix,
                  uint8 prefix_length,
                  const ::perception::network::IpAddress& next_hop,
                  size_t interface_index,
                  std::chrono::steady_clock::time_point expires_at,
                  uint32 metric = 0);

  // Removes matching routes and returns true if at least one entry was removed.
  bool RemoveRoute(
      const ::perception::network::IpAddress& prefix, uint8 prefix_length,
      std::optional<RouteOrigin> origin = std::nullopt,
      std::optional<::perception::network::IpAddress> next_hop = std::nullopt,
      std::optional<size_t> interface_index = std::nullopt);

  // Removes all routes associated with `interface_index`.
  void RemoveRoutesForInterface(size_t interface_index);

  // Removes all routes whose expiration time is at or before `now`.
  void PurgeExpired(std::chrono::steady_clock::time_point now);

  // Performs longest-prefix-match lookup for `destination`.
  std::optional<ResolvedRoute> Lookup(
      const ::perception::network::IpAddress& destination,
      std::optional<std::chrono::steady_clock::time_point> now =
          std::nullopt) const;

  // Returns all stored routes.
  const std::vector<RouteEntry>& Routes() const { return routes_; }

  // Removes all routes.
  void Clear() { routes_.clear(); }

 private:
  // Stored route entries.
  std::vector<RouteEntry> routes_;
};
