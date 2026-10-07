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

#include "address_selection.h"

#include <algorithm>
#include <array>
#include <cstring>

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

namespace {

// Escape hatch (Q13): when true, raises the ::ffff:0:0/96 precedence to 100
// so IPv4 sorts first everywhere (RFC 6724 section 10.3).
constexpr bool kPreferIpv4 = false;

// Gives the deprecated site-local fec0::/10 range global scope (14) in
// Rule 2 so a QEMU/slirp fec0::/64 source sorts global IPv6 destinations
// before IPv4. Note: ULA (fc00::/7) still has label 13 in the policy table,
// so a ULA source fails Rule 5 (matching label) for global IPv6 destinations
// and sorts IPv4 first.
constexpr bool kTreatSiteLocalAsGlobal = true;

// Precedence assigned to ::ffff:0:0/96 when prefer_ipv4 is enabled.
constexpr uint8 kPreferIpv4Precedence = 100;

// Label of the ::ffff:0:0/96 IPv4-mapped entry in the policy table.
constexpr uint8 kIpv4MappedLabel = 4;

// Entry in the RFC 6724 policy table (matching musl's defpolicy layout).
struct PolicyTableEntry {
  // Prefix bytes in network byte order.
  std::array<uint8, 16> addr;
  // Number of full bytes that must match exactly.
  uint8 len;
  // Bitmask applied to byte at index `len`.
  uint8 mask;
  // Precedence value.
  uint8 precedence;
  // Label value.
  uint8 label;
};

// RFC 6724 policy table matching musl's defpolicy in
// third_party/Libraries/musl/source/network/lookup_name.c. The deprecated
// fec0::/10, ::/96, and 3ffe::/16 entries are deliberately omitted so they
// fall through to ::/0 (precedence 40, label 1).
constexpr std::array<PolicyTableEntry, 6> kPolicyTable = {{
    // ::1/128 -> precedence 50, label 0.
    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 15, 0xFF, 50, 0},
    // ::ffff:0:0/96 -> precedence 35, label 4.
    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0}, 11, 0xFF, 35, 4},
    // 2002::/16 -> precedence 30, label 2.
    {{0x20, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 1, 0xFF, 30, 2},
    // 2001::/32 -> precedence 5, label 5.
    {{0x20, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 3, 0xFF, 5, 5},
    // fc00::/7 -> precedence 3, label 13.
    {{0xFC, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0, 0xFE, 3, 13},
    // ::/0 -> precedence 40, label 1.
    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0, 0x00, 40, 1},
}};

// Converts an IpAddress to the 16-byte IPv6 representation used for policy
// table lookup (IPv4 addresses become ::ffff:a.b.c.d).
std::array<uint8, 16> ToMappedV6Bytes(const IpAddress& address) {
  if (address.IsV4()) {
    const auto& v4 = address.bytes();
    return {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, v4[0], v4[1], v4[2], v4[3]};
  }
  return address.bytes();
}

// Returns true if `bytes` is in the deprecated site-local prefix fec0::/10.
bool IsIpv6SiteLocal(const std::array<uint8, 16>& bytes) {
  return bytes[0] == 0xFE && (bytes[1] & 0xC0) == 0xC0;
}

// Annotated destination entry for RFC 6724 section 6 sorting.
struct RankedDestination {
  // The destination address.
  IpAddress destination;
  // Original index in the input span (Rule 10 tie-breaker).
  size_t original_index = 0;
  // Selected source for reaching `destination`, or nullopt if unusable.
  std::optional<SelectedSource> source;
};

}  // namespace

AddressSelectionOptions DefaultAddressSelectionOptions() {
  AddressSelectionOptions options;
  options.prefer_ipv4 = kPreferIpv4;
  options.treat_site_local_as_global = kTreatSiteLocalAsGlobal;
  options.prefer_temporary_addresses = true;
  options.connect_strategy = ConnectStrategy::HappyEyeballs;
  return options;
}

std::vector<IpAddress> InterleaveForHappyEyeballs(
    std::span<const IpAddress> sorted_addresses, ConnectStrategy strategy) {
  if (strategy == ConnectStrategy::StrictRfc6724 ||
      sorted_addresses.size() <= 1)
    return std::vector<IpAddress>(sorted_addresses.begin(),
                                  sorted_addresses.end());

  IpAddressFamily primary_family = sorted_addresses.front().family();
  std::vector<IpAddress> primary;
  std::vector<IpAddress> secondary;
  primary.reserve(sorted_addresses.size());
  secondary.reserve(sorted_addresses.size());

  for (const IpAddress& addr : sorted_addresses) {
    if (addr.family() == primary_family)
      primary.push_back(addr);
    else
      secondary.push_back(addr);
  }

  std::vector<IpAddress> interleaved;
  interleaved.reserve(sorted_addresses.size());
  size_t p_idx = 0;
  size_t s_idx = 0;
  while (p_idx < primary.size() || s_idx < secondary.size()) {
    if (p_idx < primary.size()) interleaved.push_back(primary[p_idx++]);
    if (s_idx < secondary.size()) interleaved.push_back(secondary[s_idx++]);
  }
  return interleaved;
}

AddressSelector::AddressSelector()
    : options_(DefaultAddressSelectionOptions()) {}

AddressSelector::AddressSelector(const AddressSelectionOptions& options)
    : options_(options) {}

AddressPolicy AddressSelector::PolicyOf(const IpAddress& address) const {
  std::array<uint8, 16> bytes = ToMappedV6Bytes(address);
  for (const PolicyTableEntry& entry : kPolicyTable) {
    if (entry.len > 0 &&
        std::memcmp(bytes.data(), entry.addr.data(), entry.len) != 0)
      continue;
    if ((bytes[entry.len] & entry.mask) != entry.addr[entry.len]) continue;
    uint8 precedence = entry.precedence;
    if (options_.prefer_ipv4 && entry.label == kIpv4MappedLabel)
      precedence = kPreferIpv4Precedence;
    return {precedence, entry.label};
  }
  return {40, 1};
}

AddressScope AddressSelector::ScopeOf(const IpAddress& address) const {
  if (address.IsV4()) {
    if (address.IsLoopback() || address.IsLinkLocal())
      return AddressScope::LinkLocal;
    return AddressScope::Global;
  }
  const auto& bytes = address.bytes();
  if (address.IsMulticast())
    return static_cast<AddressScope>(bytes[1] & 0x0F);
  if (address.IsLinkLocal() || address.IsLoopback())
    return AddressScope::LinkLocal;
  if (IsIpv6SiteLocal(bytes)) {
    return options_.treat_site_local_as_global ? AddressScope::Global
                                               : AddressScope::SiteLocal;
  }
  return AddressScope::Global;
}

std::optional<SourceAddressCandidate> AddressSelector::SelectSource(
    const IpAddress& destination,
    std::span<const SourceAddressCandidate> candidates) const {
  std::optional<SourceAddressCandidate> best;
  for (const SourceAddressCandidate& candidate : candidates) {
    if (candidate.address.family() != destination.family() ||
        candidate.address.IsUnspecified())
      continue;
    if (!best || IsBetterSource(destination, candidate, *best))
      best = candidate;
  }
  return best;
}

bool AddressSelector::IsBetterSource(const IpAddress& destination,
                                     const SourceAddressCandidate& a,
                                     const SourceAddressCandidate& b) const {
  // Rule 1: Prefer same address.
  bool same_a = (a.address == destination);
  bool same_b = (b.address == destination);
  if (same_a != same_b) return same_a;

  // Rule 2: Prefer appropriate scope.
  uint8 scope_a = static_cast<uint8>(ScopeOf(a.address));
  uint8 scope_b = static_cast<uint8>(ScopeOf(b.address));
  uint8 scope_d = static_cast<uint8>(ScopeOf(destination));
  if (scope_a != scope_b) {
    if (scope_a < scope_b) return scope_a >= scope_d;
    return scope_b < scope_d;
  }

  // Rule 3: Avoid deprecated addresses.
  if (a.deprecated != b.deprecated) return !a.deprecated;

  // Rule 5: Prefer outgoing interface.
  if (a.on_outgoing_interface != b.on_outgoing_interface)
    return a.on_outgoing_interface;

  // Rule 6: Prefer matching label.
  uint8 label_d = PolicyOf(destination).label;
  bool label_a = (PolicyOf(a.address).label == label_d);
  bool label_b = (PolicyOf(b.address).label == label_d);
  if (label_a != label_b) return label_a;

  // Rule 7: Prefer temporary addresses.
  if (options_.prefer_temporary_addresses && a.temporary != b.temporary)
    return a.temporary;

  // Rule 8: Use longest matching prefix (capped at candidate prefix length).
  uint8 prefix_a =
      std::min(a.address.CommonPrefixLength(destination), a.prefix_length);
  uint8 prefix_b =
      std::min(b.address.CommonPrefixLength(destination), b.prefix_length);
  if (prefix_a != prefix_b) return prefix_a > prefix_b;

  return false;
}

std::vector<IpAddress> AddressSelector::SortDestinations(
    std::span<const IpAddress> destinations, const SourceLookup& lookup) const {
  return SortDestinations(destinations, lookup, ConnectStrategy::StrictRfc6724);
}

std::vector<IpAddress> AddressSelector::SortDestinations(
    std::span<const IpAddress> destinations, const SourceLookup& lookup,
    ConnectStrategy strategy) const {
  std::vector<RankedDestination> ranked;
  ranked.reserve(destinations.size());
  for (size_t i = 0; i < destinations.size(); i++)
    ranked.push_back(
        {destinations[i], i, lookup ? lookup(destinations[i]) : std::nullopt});

  std::sort(ranked.begin(), ranked.end(),
            [this](const RankedDestination& a, const RankedDestination& b) {
              // Rule 1: Avoid unusable destinations.
              bool usable_a = a.source.has_value();
              bool usable_b = b.source.has_value();
              if (usable_a != usable_b) return usable_a;

              // Rule 2: Prefer matching scope.
              AddressScope dst_scope_a = ScopeOf(a.destination);
              AddressScope dst_scope_b = ScopeOf(b.destination);
              bool scope_match_a =
                  usable_a && (ScopeOf(a.source->address) == dst_scope_a);
              bool scope_match_b =
                  usable_b && (ScopeOf(b.source->address) == dst_scope_b);
              if (scope_match_a != scope_match_b) return scope_match_a;

              // Rule 3: Avoid deprecated addresses.
              bool active_a = usable_a && !a.source->deprecated;
              bool active_b = usable_b && !b.source->deprecated;
              if (active_a != active_b) return active_a;

              // Rule 5: Prefer matching label.
              AddressPolicy dst_policy_a = PolicyOf(a.destination);
              AddressPolicy dst_policy_b = PolicyOf(b.destination);
              bool label_match_a =
                  usable_a &&
                  (PolicyOf(a.source->address).label == dst_policy_a.label);
              bool label_match_b =
                  usable_b &&
                  (PolicyOf(b.source->address).label == dst_policy_b.label);
              if (label_match_a != label_match_b) return label_match_a;

              // Rule 6: Prefer higher precedence.
              if (dst_policy_a.precedence != dst_policy_b.precedence)
                return dst_policy_a.precedence > dst_policy_b.precedence;

              // Rule 8: Prefer smaller scope.
              if (dst_scope_a != dst_scope_b)
                return static_cast<uint8>(dst_scope_a) <
                       static_cast<uint8>(dst_scope_b);

              // Rule 9: Use longest matching prefix (same family only).
              uint8 prefix_a =
                  (usable_a &&
                   a.source->address.family() == a.destination.family())
                      ? a.source->address.CommonPrefixLength(a.destination)
                      : 0;
              uint8 prefix_b =
                  (usable_b &&
                   b.source->address.family() == b.destination.family())
                      ? b.source->address.CommonPrefixLength(b.destination)
                      : 0;
              if (prefix_a != prefix_b) return prefix_a > prefix_b;

              // Rule 10: Otherwise, leave the order unchanged.
              return a.original_index < b.original_index;
            });

  if (strategy == ConnectStrategy::StrictRfc6724 || ranked.size() <= 1) {
    std::vector<IpAddress> result;
    result.reserve(ranked.size());
    for (const RankedDestination& entry : ranked)
      result.push_back(entry.destination);
    return result;
  }

  // Partition into usable and unusable so unusable destinations are never
  // interleaved ahead of usable ones.
  std::vector<IpAddress> usable;
  std::vector<IpAddress> unusable;
  usable.reserve(ranked.size());
  for (const RankedDestination& entry : ranked) {
    if (entry.source.has_value())
      usable.push_back(entry.destination);
    else
      unusable.push_back(entry.destination);
  }

  std::vector<IpAddress> result =
      InterleaveForHappyEyeballs(usable, ConnectStrategy::HappyEyeballs);
  std::vector<IpAddress> interleaved_unusable =
      InterleaveForHappyEyeballs(unusable, ConnectStrategy::HappyEyeballs);
  result.insert(result.end(), interleaved_unusable.begin(),
                interleaved_unusable.end());
  return result;
}
