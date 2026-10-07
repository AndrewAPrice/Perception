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

#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "perception/network/ip_address.h"

namespace {

// RFC 8305 section 5 connection attempt delay in milliseconds between
// staggered Happy Eyeballs v2 connection attempts.
constexpr uint32 kHappyEyeballsAttemptDelayMs = 250;

}  // namespace

// Strategy for ordering and attempting destination addresses (decision Q21).
enum class ConnectStrategy : uint8 {
  // RFC 8305 Happy Eyeballs v2: sort per RFC 6724, then interleave address
  // families (keeping RFC 6724's #1 choice first) and stagger attempts by
  // kHappyEyeballsAttemptDelayMs.
  HappyEyeballs = 0,
  // Strict sequential RFC 6724 order without family interleaving.
  StrictRfc6724 = 1,
};

// Precedence and label from the RFC 6724 policy table.
struct AddressPolicy {
  // Higher precedence sorts first (destination rule 6).
  uint8 precedence = 0;
  // Labels are compared between source and destination (rules 5 and 6).
  uint8 label = 0;

  bool operator==(const AddressPolicy& other) const = default;
};

// RFC 6724 / RFC 4291 scope values. Multicast addresses may carry any 4-bit
// scope value.
enum class AddressScope : uint8 {
  InterfaceLocal = 1,
  LinkLocal = 2,
  AdminLocal = 4,
  SiteLocal = 5,
  OrganizationLocal = 8,
  Global = 14
};

// Tunables of the selection algorithm.
struct AddressSelectionOptions {
  // Raises the ::ffff:0:0/96 precedence to 100 so IPv4 sorts first
  // (RFC 6724 section 10.3).
  bool prefer_ipv4 = false;
  // Gives the deprecated site-local fec0::/10 range global scope instead of
  // site-local scope so fec0::/64 sources sort global IPv6 before IPv4.
  bool treat_site_local_as_global = true;
  // Applies source rule 7 (prefer temporary addresses).
  bool prefer_temporary_addresses = true;
  // Default connection strategy (Happy Eyeballs v2 per decision Q21).
  ConnectStrategy connect_strategy = ConnectStrategy::HappyEyeballs;
};

// Returns the options built from the compile-time constants in
// address_selection.cc.
AddressSelectionOptions DefaultAddressSelectionOptions();

// Interleaves an RFC 6724-sorted destination list for Happy Eyeballs v2
// (RFC 8305 section 4): keeps the first address (RFC 6724's #1 choice) first,
// then alternates address families while preserving within-family order.
// When `strategy` is StrictRfc6724, returns `sorted_addresses` unchanged.
std::vector<::perception::network::IpAddress> InterleaveForHappyEyeballs(
    std::span<const ::perception::network::IpAddress> sorted_addresses,
    ConnectStrategy strategy = ConnectStrategy::HappyEyeballs);

// An address that may be used as a source.
struct SourceAddressCandidate {
  // The assigned address. Only Preferred and Deprecated addresses should be
  // offered; tentative and duplicate ones are not usable.
  ::perception::network::IpAddress address;
  // On-link prefix length; limits the prefix comparison of source rule 8.
  uint8 prefix_length = 128;
  // True if the address's preferred lifetime has ended.
  bool deprecated = false;
  // True if the address is an RFC 8981 temporary address.
  bool temporary = false;
  // True if the address is assigned to the interface the destination is
  // routed through.
  bool on_outgoing_interface = true;
};

// The source that would be used to reach a destination.
struct SelectedSource {
  // The source address.
  ::perception::network::IpAddress address;
  // True if the source address is deprecated.
  bool deprecated = false;
};

// Returns the source used to reach a destination, or nullopt if the
// destination is unusable (no route or no source address).
using SourceLookup = std::function<std::optional<SelectedSource>(
    const ::perception::network::IpAddress& destination)>;

// RFC 6724 source address selection and destination address sorting using
// musl's policy table (third_party/Libraries/musl/source/network/
// lookup_name.c, defpolicy), so the service and musl's getaddrinfo agree.
class AddressSelector {
 public:
  // Uses DefaultAddressSelectionOptions().
  AddressSelector();

  explicit AddressSelector(const AddressSelectionOptions& options);

  // Returns the policy table entry for `address`. IPv4 addresses are looked up
  // in their IPv4-mapped form.
  AddressPolicy PolicyOf(const ::perception::network::IpAddress& address) const;

  // Returns the scope of `address`.
  AddressScope ScopeOf(const ::perception::network::IpAddress& address) const;

  // Selects the best source for `destination` from `candidates` per RFC 6724
  // section 5. Only candidates of the destination's family are considered.
  // Returns nullopt if no candidate is usable.
  std::optional<SourceAddressCandidate> SelectSource(
      const ::perception::network::IpAddress& destination,
      std::span<const SourceAddressCandidate> candidates) const;

  // Sorts `destinations` strictly per RFC 6724 section 6 (rules 1, 2, 3, 5, 6,
  // 8, 9; ties keep the input order). `lookup` is called once per destination.
  std::vector<::perception::network::IpAddress> SortDestinations(
      std::span<const ::perception::network::IpAddress> destinations,
      const SourceLookup& lookup) const;

  // Sorts `destinations` per RFC 6724 section 6 and, if `strategy` is
  // HappyEyeballs, interleaves usable destinations by address family per
  // RFC 8305 section 4 while keeping unusable destinations at the end.
  std::vector<::perception::network::IpAddress> SortDestinations(
      std::span<const ::perception::network::IpAddress> destinations,
      const SourceLookup& lookup, ConnectStrategy strategy) const;

 private:
  // Returns true if source `a` is preferred over source `b` for
  // `destination`.
  bool IsBetterSource(const ::perception::network::IpAddress& destination,
                      const SourceAddressCandidate& a,
                      const SourceAddressCandidate& b) const;

  // The algorithm's tunables.
  AddressSelectionOptions options_;
};
