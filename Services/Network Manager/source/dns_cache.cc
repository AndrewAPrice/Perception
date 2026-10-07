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

#include "dns_cache.h"

#include <algorithm>

namespace {

// Shortest TTL honored, in seconds.
constexpr uint32 kMinimumTtlSeconds = 5;

// Longest TTL honored, in seconds (one day).
constexpr uint32 kMaximumTtlSeconds = 86400;

// Negative caching TTL used when the response has no SOA record, in seconds.
constexpr uint32 kDefaultNegativeTtlSeconds = 30;

// Maximum number of (name, type) entries kept.
constexpr size_t kMaxEntries = 1024;

// Clamps a TTL to the honored range and converts it to a duration.
std::chrono::seconds ClampedTtl(uint32 ttl) {
  return std::chrono::seconds(
      std::clamp(ttl, kMinimumTtlSeconds, kMaximumTtlSeconds));
}

}  // namespace

std::chrono::steady_clock::time_point DnsCache::Entry::LastExpiry() const {
  if (addresses.empty()) return negative_expires;
  std::chrono::steady_clock::time_point last = addresses.front().expires;
  for (const CachedAddress& cached : addresses)
    last = std::max(last, cached.expires);
  return last;
}

bool DnsCache::Store(std::string_view name, DnsRecordType type,
                     const DnsResponse& response,
                     std::chrono::steady_clock::time_point now) {
  Entry entry;
  if (!response.addresses.empty()) {
    for (const DnsAddressRecord& record : response.addresses)
      entry.addresses.push_back({record.address, now + ClampedTtl(record.ttl)});
  } else if (response.response_code == DnsResponseCode::NameError ||
             response.response_code == DnsResponseCode::NoError) {
    entry.negative_expires =
        now +
        ClampedTtl(response.negative_ttl.value_or(kDefaultNegativeTtlSeconds));
  } else {
    return false;
  }

  auto key = std::make_pair(CanonicalDnsName(name), type);
  if (!entries_.contains(key) && entries_.size() >= kMaxEntries) MakeRoom(now);
  entries_[key] = std::move(entry);
  return true;
}

std::optional<DnsCache::Result> DnsCache::Lookup(
    std::string_view name, DnsRecordType type,
    std::chrono::steady_clock::time_point now) const {
  auto it = entries_.find(std::make_pair(CanonicalDnsName(name), type));
  if (it == entries_.end()) return std::nullopt;
  const Entry& entry = it->second;

  Result result;
  if (entry.addresses.empty()) {
    if (entry.negative_expires <= now) return std::nullopt;
    result.negative = true;
    return result;
  }
  for (const CachedAddress& cached : entry.addresses) {
    if (cached.expires <= now) continue;
    auto remaining =
        std::chrono::duration_cast<std::chrono::seconds>(cached.expires - now);
    result.addresses.push_back(
        {cached.address, static_cast<uint32>(remaining.count())});
  }
  if (result.addresses.empty()) return std::nullopt;
  return result;
}

void DnsCache::Purge(std::chrono::steady_clock::time_point now) {
  std::erase_if(entries_, [now](const auto& key_and_entry) {
    return key_and_entry.second.LastExpiry() <= now;
  });
}

void DnsCache::MakeRoom(std::chrono::steady_clock::time_point now) {
  Purge(now);
  if (entries_.size() < kMaxEntries) return;
  auto soonest = std::min_element(
      entries_.begin(), entries_.end(), [](const auto& a, const auto& b) {
        return a.second.LastExpiry() < b.second.LastExpiry();
      });
  entries_.erase(soonest);
}
