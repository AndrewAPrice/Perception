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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dns_message.h"

// A TTL-honoring cache of DNS answers keyed by (canonical name, record type).
// Positive TTLs are clamped to [5 s, 1 day]. Negative answers (NXDOMAIN or
// NODATA) are cached for the SOA-derived TTL, or 30 s without an SOA, with
// the same clamp. The clock is injected through the `now` parameters.
class DnsCache {
 public:
  // The cached outcome of a lookup.
  struct Result {
    // Unexpired addresses with their remaining TTL in seconds. Empty for a
    // negative result.
    std::vector<DnsAddressRecord> addresses;
    // True if the name is cached as having no records of the type.
    bool negative = false;
  };

  // Caches the outcome of `response` for `name` and `type`. Responses with
  // addresses are cached positively; NXDOMAIN and empty NOERROR responses are
  // cached negatively; other response codes are not cached. Returns true if
  // anything was stored.
  bool Store(std::string_view name, DnsRecordType type,
             const DnsResponse& response,
             std::chrono::steady_clock::time_point now);

  // Returns the cached result for `name` and `type`, or nullopt on a miss or
  // if every entry has expired.
  std::optional<Result> Lookup(std::string_view name, DnsRecordType type,
                               std::chrono::steady_clock::time_point now) const;

  // Removes all expired entries.
  void Purge(std::chrono::steady_clock::time_point now);

  // Returns the number of cached (name, type) entries, expired or not.
  size_t Size() const { return entries_.size(); }

 private:
  // A cached address and its expiry.
  struct CachedAddress {
    // The address.
    ::perception::network::IpAddress address;
    // When the address expires.
    std::chrono::steady_clock::time_point expires;
  };

  // A cached answer for one (name, type).
  struct Entry {
    // Positive addresses, empty for negative entries.
    std::vector<CachedAddress> addresses;
    // When a negative entry expires (unused for positive entries).
    std::chrono::steady_clock::time_point negative_expires;

    // Returns the latest time at which any part of the entry is valid.
    std::chrono::steady_clock::time_point LastExpiry() const;
  };

  // Makes room for one more entry by purging, then evicting the entry that
  // expires first.
  void MakeRoom(std::chrono::steady_clock::time_point now);

  // Cache entries keyed by canonical name and record type.
  std::map<std::pair<std::string, DnsRecordType>, Entry> entries_;
};
