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
#include <map>
#include <optional>

#include "perception/network/ip_address.h"

// Path MTU Discovery cache shared by IPv4 (RFC 1191) and IPv6 (RFC 8201).
// Entries expire after 10 minutes so the stack periodically probes whether a
// higher link MTU has become usable again.
class PmtuCache {
 public:
  // Returns the effective Path MTU for `destination` given `link_mtu` at
  // `now`. If no unexpired entry exists, returns `link_mtu`.
  uint16 GetPathMtu(const ::perception::network::IpAddress& destination,
                    uint16 link_mtu,
                    std::chrono::steady_clock::time_point now) const;

  // Applies an ICMPv6 Packet Too Big or ICMPv4 Fragmentation Needed report
  // for `destination`.
  // - For IPv6: values below 1280 are ignored per RFC 8201 section 4 and this
  //   project's security policy (section 9.4 / 10).
  // - For IPv4: a `reported_mtu` of 0 steps down using the RFC 1191 plateau
  //   table; positive values below 68 are clamped to the 68-byte IPv4 floor.
  // - Reports that do not reduce the current Path MTU below `link_mtu` are
  //   ignored.
  // Returns the newly cached Path MTU if the cache was updated, or nullopt if
  // the report was ignored.
  std::optional<uint16> OnPacketTooBig(
      const ::perception::network::IpAddress& destination, uint32 reported_mtu,
      uint16 link_mtu, std::chrono::steady_clock::time_point now);

  // Removes all expired entries.
  void Purge(std::chrono::steady_clock::time_point now);

  // Returns the number of cached entries (expired or not).
  size_t Size() const { return entries_.size(); }

 private:
  // A cached PMTU estimate for a single destination.
  struct Entry {
    // Cached Path MTU in bytes.
    uint16 mtu = 0;
    // Time at which the entry expires (10 minutes after last update).
    std::chrono::steady_clock::time_point expires;
  };

  // Purges expired entries and, if still at capacity, evicts the entry that
  // expires soonest.
  void MakeRoom(std::chrono::steady_clock::time_point now);

  // Cached PMTU entries keyed by destination IP address.
  std::map<::perception::network::IpAddress, Entry> entries_;
};
