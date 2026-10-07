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

#include <array>
#include <vector>

#include "testing.h"

using ::perception::network::IpAddress;

namespace {

// Helper to parse an address literal in tests.
IpAddress Addr(std::string_view text) { return *IpAddress::Parse(text); }

TEST(AddressSelection_MuslPolicyTableParity) {
  AddressSelector selector;

  // ::1/128 -> precedence 50, label 0.
  EXPECT((AddressPolicy{50, 0}), selector.PolicyOf(Addr("::1")));

  // ::ffff:0:0/96 (and V4 addresses) -> precedence 35, label 4.
  EXPECT((AddressPolicy{35, 4}), selector.PolicyOf(IpAddress::V4(10, 0, 2, 15)));
  EXPECT((AddressPolicy{35, 4}), selector.PolicyOf(IpAddress::V4(93, 184, 216, 34)));

  // 2002::/16 (6to4) -> precedence 30, label 2.
  EXPECT((AddressPolicy{30, 2}), selector.PolicyOf(Addr("2002:c000:0204::1")));

  // 2001::/32 (Teredo) -> precedence 5, label 5.
  EXPECT((AddressPolicy{5, 5}), selector.PolicyOf(Addr("2001:0000:4136:e378::1")));

  // fc00::/7 (ULA, both fc00::/8 and fd00::/8) -> precedence 3, label 13.
  EXPECT((AddressPolicy{3, 13}), selector.PolicyOf(Addr("fc00::1")));
  EXPECT((AddressPolicy{3, 13}), selector.PolicyOf(Addr("fd12:3456:789a::1")));

  // ::/0 (global unicast) -> precedence 40, label 1.
  EXPECT((AddressPolicy{40, 1}), selector.PolicyOf(Addr("2607:f8b0:4004:800::200e")));
  EXPECT((AddressPolicy{40, 1}), selector.PolicyOf(Addr("2001:4860:4860::8888")));

  // Omitted deprecated prefixes (fec0::/10, ::/96, 3ffe::/16) must fall through
  // to ::/0 (precedence 40, label 1), matching musl's #if 0 block.
  EXPECT((AddressPolicy{40, 1}), selector.PolicyOf(Addr("fec0::5054:ff:fe12:3456")));
  EXPECT((AddressPolicy{40, 1}), selector.PolicyOf(Addr("3ffe::1")));
  IpAddress v4_compat = IpAddress::V6({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4});
  EXPECT((AddressPolicy{40, 1}), selector.PolicyOf(v4_compat));
}

TEST(AddressSelection_Fec0SourceSortsGlobalIpv6First) {
  AddressSelector selector;
  IpAddress v6_src = Addr("fec0::5054:ff:fe12:3456");
  IpAddress v4_src = IpAddress::V4(10, 0, 2, 15);
  IpAddress v6_dst = Addr("2607:f8b0:4004:800::200e");
  IpAddress v4_dst = IpAddress::V4(142, 250, 80, 46);

  // With default options (kTreatSiteLocalAsGlobal = true), fec0::/10 has
  // global scope (14), matching global IPv6 destinations in Rule 2 and label 1
  // in Rule 5, so precedence 40 beats IPv4's 35.
  EXPECT(AddressScope::Global, selector.ScopeOf(v6_src));

  std::vector<IpAddress> destinations = {v4_dst, v6_dst};
  auto sorted = selector.SortDestinations(
      destinations, [&](const IpAddress& dst) -> std::optional<SelectedSource> {
        return SelectedSource{dst.IsV6() ? v6_src : v4_src, false};
      });

  ASSERT(static_cast<size_t>(2), sorted.size());
  EXPECT(v6_dst, sorted[0]);
  EXPECT(v4_dst, sorted[1]);
}

TEST(AddressSelection_UlaSourceSortsIpv4First) {
  AddressSelector selector;
  IpAddress ula_src = Addr("fd00::5054:ff:fe12:3456");
  IpAddress v4_src = IpAddress::V4(10, 0, 2, 15);
  IpAddress v6_dst = Addr("2607:f8b0:4004:800::200e");
  IpAddress v4_dst = IpAddress::V4(142, 250, 80, 46);

  // ULA source has label 13, which does not match global v6_dst (label 1),
  // whereas v4_src and v4_dst both have label 4. Rule 5 therefore sorts IPv4
  // before IPv6.
  std::vector<IpAddress> destinations = {v6_dst, v4_dst};
  auto sorted = selector.SortDestinations(
      destinations, [&](const IpAddress& dst) -> std::optional<SelectedSource> {
        return SelectedSource{dst.IsV6() ? ula_src : v4_src, false};
      });

  ASSERT(static_cast<size_t>(2), sorted.size());
  EXPECT(v4_dst, sorted[0]);
  EXPECT(v6_dst, sorted[1]);
}

TEST(AddressSelection_PreferIpv4EscapeHatch) {
  AddressSelectionOptions options = DefaultAddressSelectionOptions();
  options.prefer_ipv4 = true;
  AddressSelector selector(options);

  EXPECT((AddressPolicy{100, 4}), selector.PolicyOf(IpAddress::V4(10, 0, 2, 15)));

  IpAddress v6_src = Addr("2001:db8::2");
  IpAddress v4_src = IpAddress::V4(10, 0, 2, 15);
  IpAddress v6_dst = Addr("2607:f8b0:4004:800::200e");
  IpAddress v4_dst = IpAddress::V4(142, 250, 80, 46);

  std::vector<IpAddress> destinations = {v6_dst, v4_dst};
  auto sorted = selector.SortDestinations(
      destinations, [&](const IpAddress& dst) -> std::optional<SelectedSource> {
        return SelectedSource{dst.IsV6() ? v6_src : v4_src, false};
      });

  ASSERT(static_cast<size_t>(2), sorted.size());
  EXPECT(v4_dst, sorted[0]);
  EXPECT(v6_dst, sorted[1]);
}

TEST(AddressSelection_SourceSelectionRules) {
  AddressSelector selector;
  IpAddress dst_global = Addr("2001:4860:4860::8888");
  IpAddress dst_link = Addr("fe80::2");

  // Rule 1: Prefer same address.
  std::vector<SourceAddressCandidate> rule1_candidates = {
      {Addr("2001:4860:4860::1"), 64, false, false, true},
      {dst_global, 64, false, false, true},
  };
  auto rule1 = selector.SelectSource(dst_global, rule1_candidates);
  ASSERT(true, rule1.has_value());
  EXPECT(dst_global, rule1->address);

  // Rule 2: Prefer appropriate scope (link-local source for link-local dest,
  // global source for global dest).
  std::vector<SourceAddressCandidate> scope_candidates = {
      {Addr("fe80::1"), 64, false, false, true},
      {Addr("2001:db8::1"), 64, false, false, true},
  };
  EXPECT(Addr("fe80::1"),
         selector.SelectSource(dst_link, scope_candidates)->address);
  EXPECT(Addr("2001:db8::1"),
         selector.SelectSource(dst_global, scope_candidates)->address);

  // Rule 3: Avoid deprecated addresses.
  std::vector<SourceAddressCandidate> dep_candidates = {
      {Addr("2001:4860:4860::1"), 64, true, false, true},
      {Addr("2001:db8::1"), 64, false, false, true},
  };
  EXPECT(Addr("2001:db8::1"),
         selector.SelectSource(dst_global, dep_candidates)->address);

  // Rule 5: Prefer outgoing interface.
  std::vector<SourceAddressCandidate> iface_candidates = {
      {Addr("2001:db8::1"), 64, false, false, false},
      {Addr("2001:db8::2"), 64, false, false, true},
  };
  EXPECT(Addr("2001:db8::2"),
         selector.SelectSource(dst_global, iface_candidates)->address);

  // Rule 6: Prefer matching label (global source over ULA source for global dest).
  std::vector<SourceAddressCandidate> label_candidates = {
      {Addr("fd00::1"), 64, false, false, true},
      {Addr("2001:db8::1"), 64, false, false, true},
  };
  EXPECT(Addr("2001:db8::1"),
         selector.SelectSource(dst_global, label_candidates)->address);

  // Rule 7: Prefer temporary addresses.
  std::vector<SourceAddressCandidate> temp_candidates = {
      {Addr("2001:db8::1"), 64, false, false, true},
      {Addr("2001:db8::9999"), 64, false, true, true},
  };
  EXPECT(Addr("2001:db8::9999"),
         selector.SelectSource(dst_global, temp_candidates)->address);

  // Rule 8: Longest matching prefix capped at candidate prefix_length.
  std::vector<SourceAddressCandidate> prefix_candidates = {
      {Addr("2001:4800::1"), 32, false, false, true},
      {Addr("2001:4860:4860::1"), 64, false, false, true},
  };
  EXPECT(Addr("2001:4860:4860::1"),
         selector.SelectSource(dst_global, prefix_candidates)->address);
}

TEST(AddressSelection_DestinationSortingAllRules) {
  AddressSelector selector;
  IpAddress v6_src = Addr("2001:db8:1::1");
  IpAddress v4_src = IpAddress::V4(10, 0, 2, 15);

  // Rule 1: Unusable destinations sort last.
  IpAddress usable_v4 = IpAddress::V4(93, 184, 216, 34);
  IpAddress unusable_v6 = Addr("2001:db8:1::2");
  auto r1 = selector.SortDestinations(
      std::array{unusable_v6, usable_v4},
      [&](const IpAddress& dst) -> std::optional<SelectedSource> {
        if (dst.IsV6()) return std::nullopt;
        return SelectedSource{v4_src, false};
      });
  EXPECT(usable_v4, r1[0]);
  EXPECT(unusable_v6, r1[1]);

  // Rule 3: Avoid deprecated source.
  auto r3 = selector.SortDestinations(
      std::array{unusable_v6, usable_v4},
      [&](const IpAddress& dst) -> std::optional<SelectedSource> {
        if (dst.IsV6()) return SelectedSource{v6_src, true};
        return SelectedSource{v4_src, false};
      });
  EXPECT(usable_v4, r3[0]);
  EXPECT(unusable_v6, r3[1]);

  // Rule 8: Prefer smaller scope when precedence/labels tie.
  IpAddress link_local_dst = Addr("fe80::99");
  IpAddress global_dst = Addr("2001:db8:2::99");
  IpAddress link_local_src = Addr("fe80::1");
  auto r8 = selector.SortDestinations(
      std::array{global_dst, link_local_dst},
      [&](const IpAddress& dst) -> std::optional<SelectedSource> {
        return SelectedSource{dst.IsLinkLocal() ? link_local_src : v6_src, false};
      });
  EXPECT(link_local_dst, r8[0]);
  EXPECT(global_dst, r8[1]);

  // Rule 9: Longest matching prefix (same family) & Rule 10: preserve input order.
  IpAddress closer_v6 = Addr("2001:db8:1::99");
  IpAddress farther_v6_a = Addr("2606:4700::1");
  IpAddress farther_v6_b = Addr("2606:4700::2");
  auto r9 = selector.SortDestinations(
      std::array{farther_v6_a, farther_v6_b, closer_v6},
      [&](const IpAddress&) -> std::optional<SelectedSource> {
        return SelectedSource{v6_src, false};
      });
  EXPECT(closer_v6, r9[0]);
  EXPECT(farther_v6_a, r9[1]);
  EXPECT(farther_v6_b, r9[2]);
}

TEST(AddressSelection_HappyEyeballsInterleaving) {
  EXPECT(static_cast<uint32>(250), kHappyEyeballsAttemptDelayMs);
  EXPECT(ConnectStrategy::HappyEyeballs,
         DefaultAddressSelectionOptions().connect_strategy);

  IpAddress v6_1 = Addr("2607:f8b0:4004:800::1");
  IpAddress v6_2 = Addr("2607:f8b0:4004:800::2");
  IpAddress v6_3 = Addr("2607:f8b0:4004:800::3");
  IpAddress v4_1 = IpAddress::V4(142, 250, 80, 1);
  IpAddress v4_2 = IpAddress::V4(142, 250, 80, 2);

  std::vector<IpAddress> rfc6724_sorted = {v6_1, v6_2, v6_3, v4_1, v4_2};

  // Happy Eyeballs v2 keeps #1 choice (v6_1) first, then alternates families.
  auto interleaved =
      InterleaveForHappyEyeballs(rfc6724_sorted, ConnectStrategy::HappyEyeballs);
  ASSERT(static_cast<size_t>(5), interleaved.size());
  EXPECT(v6_1, interleaved[0]);
  EXPECT(v4_1, interleaved[1]);
  EXPECT(v6_2, interleaved[2]);
  EXPECT(v4_2, interleaved[3]);
  EXPECT(v6_3, interleaved[4]);

  // StrictRfc6724 preserves the sorted order unchanged.
  auto strict =
      InterleaveForHappyEyeballs(rfc6724_sorted, ConnectStrategy::StrictRfc6724);
  ASSERT(static_cast<size_t>(5), strict.size());
  EXPECT(v6_1, strict[0]);
  EXPECT(v6_2, strict[1]);
  EXPECT(v6_3, strict[2]);
  EXPECT(v4_1, strict[3]);
  EXPECT(v4_2, strict[4]);

  // SortDestinations with HappyEyeballs keeps unusable addresses at the end
  // rather than interleaving an unusable family ahead of a usable one.
  AddressSelector selector;
  IpAddress v4_src = IpAddress::V4(10, 0, 2, 15);
  std::vector<IpAddress> mixed = {v6_1, v6_2, v4_1, v4_2};
  auto v6_unusable = selector.SortDestinations(
      mixed,
      [&](const IpAddress& dst) -> std::optional<SelectedSource> {
        if (dst.IsV6()) return std::nullopt;
        return SelectedSource{v4_src, false};
      },
      ConnectStrategy::HappyEyeballs);
  ASSERT(static_cast<size_t>(4), v6_unusable.size());
  EXPECT(v4_1, v6_unusable[0]);
  EXPECT(v4_2, v6_unusable[1]);
  EXPECT(v6_1, v6_unusable[2]);
  EXPECT(v6_2, v6_unusable[3]);
}

}  // namespace
