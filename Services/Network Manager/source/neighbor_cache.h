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
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "perception/network/ip_address.h"

namespace perception {
class Fiber;
}  // namespace perception

// Reachability state of a neighbor entry (RFC 4861 §7.3.2, shared with ARP).
enum class NeighborState {
  Incomplete,
  Reachable,
  Stale,
  Delay,
  Probe,
};

// A cached mapping from an IPv4 or IPv6 next-hop address to a link-layer MAC.
struct NeighborEntry {
  // Resolved link-layer MAC address (valid in all states except Incomplete).
  std::array<uint8, 6> mac{};
  // Current neighbor reachability state.
  NeighborState state = NeighborState::Incomplete;
  // Expiration timestamp for the current state (e.g. Reachable -> Stale).
  std::chrono::steady_clock::time_point expires =
      std::chrono::steady_clock::time_point::max();
  // Timestamp of the most recent lookup or update (used for LRU eviction).
  std::chrono::steady_clock::time_point last_used =
      std::chrono::steady_clock::time_point::min();
  // Fibers waiting for address resolution to complete.
  std::vector<::perception::Fiber*> waiters;
  // Number of unanswered ARP/NS probes sent in the current resolution/probe
  // cycle.
  int probes = 0;
  // Outbound packets queued while resolution is Incomplete (capped at 3).
  std::deque<std::string> pending_packets;
  // True if the neighbor is known to be an IPv6 router.
  bool is_router = false;
};

// Per-interface neighbor cache shared by ARP (IPv4) and NDP (IPv6).
class NeighborCache {
 public:
  // Maximum number of entries stored in the cache before LRU eviction.
  static constexpr size_t kMaxEntries = 256;

  // Maximum number of packets queued per Incomplete neighbor entry.
  static constexpr size_t kMaxPendingPackets = 3;

  // Default Reachable state duration.
  static constexpr auto kDefaultReachableLifetime = std::chrono::seconds(30);

  // Result returned when an Incomplete entry resolves to a link-layer address.
  struct ResolutionResult {
    // Resolved link-layer MAC address.
    std::array<uint8, 6> mac{};
    // Waiting fibers that should be woken up.
    std::vector<::perception::Fiber*> waiters_to_wake;
    // Queued packets that can now be transmitted to `mac`.
    std::deque<std::string> pending_packets;
  };

  // Returns the link-layer MAC address for `ip` if the entry exists and has a
  // known MAC (any state other than Incomplete), updating its LRU timestamp.
  std::optional<std::array<uint8, 6>> LookupMac(
      const ::perception::network::IpAddress& ip,
      std::chrono::steady_clock::time_point now =
          std::chrono::steady_clock::now());

  // Returns a pointer to the entry for `ip`, or nullptr if not present.
  const NeighborEntry* Find(const ::perception::network::IpAddress& ip) const;

  // Returns a mutable pointer to the entry for `ip`, or nullptr if not present.
  NeighborEntry* FindMutable(const ::perception::network::IpAddress& ip);

  // Returns the entry for `ip`, creating an Incomplete entry (and evicting the
  // least-recently-used entry if at capacity) when none exists.
  NeighborEntry& EnsureIncomplete(
      const ::perception::network::IpAddress& ip,
      std::chrono::steady_clock::time_point now =
          std::chrono::steady_clock::now());

  // Registers `waiter` on `ip` (creating an Incomplete entry if needed).
  void AddWaiter(const ::perception::network::IpAddress& ip,
                 ::perception::Fiber* waiter,
                 std::chrono::steady_clock::time_point now =
                     std::chrono::steady_clock::now());

  // Removes `waiter` from `ip`'s waiter list if present.
  void RemoveWaiter(const ::perception::network::IpAddress& ip,
                    ::perception::Fiber* waiter);

  // Queues `packet` on an Incomplete entry for `ip`, dropping the oldest
  // queued packet if already at kMaxPendingPackets.
  void EnqueuePendingPacket(const ::perception::network::IpAddress& ip,
                            std::string packet,
                            std::chrono::steady_clock::time_point now =
                                std::chrono::steady_clock::now());

  // Marks `ip` as Reachable with `mac`, draining any waiting fibers and queued
  // packets into the returned ResolutionResult.
  ResolutionResult UpdateReachable(
      const ::perception::network::IpAddress& ip,
      const std::array<uint8, 6>& mac,
      std::chrono::steady_clock::time_point now =
          std::chrono::steady_clock::now(),
      std::chrono::milliseconds reachable_lifetime = kDefaultReachableLifetime);

  // Records a link-layer address learned from an unsolicited source (such as
  // an incoming ARP request or an NDP Source Link-Layer Address option). If
  // `create_if_missing` is false and `ip` is not in the cache, does nothing.
  ResolutionResult RecordUnsolicitedAddress(
      const ::perception::network::IpAddress& ip,
      const std::array<uint8, 6>& mac, bool create_if_missing = true,
      std::chrono::steady_clock::time_point now =
          std::chrono::steady_clock::now());

  // Removes the entry for `ip`. Returns true if an entry was removed.
  bool Remove(const ::perception::network::IpAddress& ip);

  // Returns the number of entries currently in the cache.
  size_t Size() const { return entries_.size(); }

  // Removes all entries from the cache.
  void Clear() { entries_.clear(); }

  // Read-only access to the underlying map for diagnostics and maintenance.
  const std::map<::perception::network::IpAddress, NeighborEntry>& entries()
      const {
    return entries_;
  }

 private:
  // Evicts the least-recently-used entry if `entries_.size() >= kMaxEntries`.
  void EvictLruIfNeeded();

  std::map<::perception::network::IpAddress, NeighborEntry> entries_;
};
