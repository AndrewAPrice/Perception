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

#include "neighbor_cache.h"

#include <algorithm>
#include <utility>

using ::perception::network::IpAddress;

std::optional<std::array<uint8, 6>> NeighborCache::LookupMac(
    const IpAddress& ip, std::chrono::steady_clock::time_point now) {
  auto it = entries_.find(ip);
  if (it == entries_.end()) return std::nullopt;
  if (it->second.state == NeighborState::Incomplete) return std::nullopt;

  if (it->second.state == NeighborState::Reachable &&
      now >= it->second.expires) {
    it->second.state = NeighborState::Stale;
  }
  it->second.last_used = now;
  return it->second.mac;
}

const NeighborEntry* NeighborCache::Find(const IpAddress& ip) const {
  auto it = entries_.find(ip);
  if (it == entries_.end()) return nullptr;
  return &it->second;
}

NeighborEntry* NeighborCache::FindMutable(const IpAddress& ip) {
  auto it = entries_.find(ip);
  if (it == entries_.end()) return nullptr;
  return &it->second;
}

NeighborEntry& NeighborCache::EnsureIncomplete(
    const IpAddress& ip, std::chrono::steady_clock::time_point now) {
  auto it = entries_.find(ip);
  if (it != entries_.end()) {
    it->second.last_used = now;
    return it->second;
  }

  EvictLruIfNeeded();
  NeighborEntry& entry = entries_[ip];
  entry.state = NeighborState::Incomplete;
  entry.last_used = now;
  return entry;
}

void NeighborCache::AddWaiter(const IpAddress& ip, ::perception::Fiber* waiter,
                              std::chrono::steady_clock::time_point now) {
  if (waiter == nullptr) return;
  NeighborEntry& entry = EnsureIncomplete(ip, now);
  if (std::find(entry.waiters.begin(), entry.waiters.end(), waiter) ==
      entry.waiters.end()) {
    entry.waiters.push_back(waiter);
  }
}

void NeighborCache::RemoveWaiter(const IpAddress& ip,
                                 ::perception::Fiber* waiter) {
  auto it = entries_.find(ip);
  if (it == entries_.end()) return;
  auto& waiters = it->second.waiters;
  waiters.erase(std::remove(waiters.begin(), waiters.end(), waiter),
                waiters.end());
}

void NeighborCache::EnqueuePendingPacket(
    const IpAddress& ip, std::string packet,
    std::chrono::steady_clock::time_point now) {
  NeighborEntry& entry = EnsureIncomplete(ip, now);
  if (entry.state != NeighborState::Incomplete) return;
  while (entry.pending_packets.size() >= kMaxPendingPackets)
    entry.pending_packets.pop_front();
  entry.pending_packets.push_back(std::move(packet));
}

NeighborCache::ResolutionResult NeighborCache::UpdateReachable(
    const IpAddress& ip, const std::array<uint8, 6>& mac,
    std::chrono::steady_clock::time_point now,
    std::chrono::milliseconds reachable_lifetime) {
  auto it = entries_.find(ip);
  if (it == entries_.end()) {
    EvictLruIfNeeded();
    it = entries_.emplace(ip, NeighborEntry{}).first;
  }

  NeighborEntry& entry = it->second;
  entry.mac = mac;
  entry.state = NeighborState::Reachable;
  entry.expires = now + reachable_lifetime;
  entry.last_used = now;
  entry.probes = 0;

  ResolutionResult result;
  result.mac = mac;
  result.waiters_to_wake = std::move(entry.waiters);
  entry.waiters.clear();
  result.pending_packets = std::move(entry.pending_packets);
  entry.pending_packets.clear();
  return result;
}

NeighborCache::ResolutionResult NeighborCache::RecordUnsolicitedAddress(
    const IpAddress& ip, const std::array<uint8, 6>& mac,
    bool create_if_missing, std::chrono::steady_clock::time_point now) {
  ResolutionResult result;
  result.mac = mac;

  auto it = entries_.find(ip);
  if (it == entries_.end()) {
    if (!create_if_missing) return result;
    EvictLruIfNeeded();
    NeighborEntry& entry = entries_[ip];
    entry.mac = mac;
    entry.state = NeighborState::Stale;
    entry.last_used = now;
    return result;
  }

  NeighborEntry& entry = it->second;
  entry.last_used = now;
  if (entry.state == NeighborState::Incomplete) {
    entry.mac = mac;
    entry.state = NeighborState::Stale;
    entry.probes = 0;
    result.waiters_to_wake = std::move(entry.waiters);
    entry.waiters.clear();
    result.pending_packets = std::move(entry.pending_packets);
    entry.pending_packets.clear();
  } else if (entry.mac != mac) {
    entry.mac = mac;
    entry.state = NeighborState::Stale;
  }
  return result;
}

bool NeighborCache::Remove(const IpAddress& ip) {
  return entries_.erase(ip) > 0;
}

void NeighborCache::EvictLruIfNeeded() {
  if (entries_.size() < kMaxEntries) return;

  auto oldest_it = entries_.begin();
  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    if (it->second.last_used < oldest_it->second.last_used) oldest_it = it;
  }
  entries_.erase(oldest_it);
}
