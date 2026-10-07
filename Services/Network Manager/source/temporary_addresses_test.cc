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

#include "temporary_addresses.h"

#include "slaac.h"
#include "testing.h"

namespace {

using ::perception::network::IpAddress;

// Sample QEMU MAC address (52:54:00:12:34:56).
constexpr HardwareAddress kTestMac = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};

TEST(ReservedTemporaryInterfaceIdentifiers) {
  EXPECT(true, IsReservedTemporaryInterfaceIdentifier({0, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT(true, IsReservedTemporaryInterfaceIdentifier(
                   {0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80}));
  EXPECT(true, IsReservedTemporaryInterfaceIdentifier(
                   {0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}));
  EXPECT(false, IsReservedTemporaryInterfaceIdentifier(
                    {0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f}));
  EXPECT(true, IsReservedTemporaryInterfaceIdentifier(
                   {0x02, 0x00, 0x5e, 0xff, 0xfe, 0x00, 0x00, 0x01}));
  EXPECT(false, IsReservedTemporaryInterfaceIdentifier(
                    {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0}));

  const auto stable_iid = ModifiedEui64InterfaceIdentifier(kTestMac);
  size_t call_count = 0;
  const auto generated = GenerateTemporaryInterfaceIdentifier(stable_iid, [&]() {
    ++call_count;
    if (call_count == 1) return 0ULL;
    return 0x1122334455667788ULL;
  });
  EXPECT(2u, call_count);
  EXPECT(0x11u, generated[0]);
  EXPECT(0x88u, generated[7]);
}

TEST(TemporaryAddressesDisabledByDefault) {
  EXPECT(false, kEnableTemporaryAddresses);
  std::vector<IpAddress> dad_probes;
  TemporaryAddressManager mgr(
      kTestMac, [&](const IpAddress& target) { dad_probes.push_back(target); });

  NdpPrefixInformation pio;
  pio.prefix = *IpAddress::Parse("fec0::");
  pio.prefix_length = 64;
  pio.autonomous = true;
  pio.valid_lifetime_seconds = 86400;
  pio.preferred_lifetime_seconds = 14400;

  const auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(10));
  mgr.OnPrefixInformation(pio, t0);
  EXPECT(0u, mgr.addresses().size());
  EXPECT(0u, dad_probes.size());
}

TEST(TemporaryAddressGenerationRegenerationAndDadRetry) {
  std::vector<IpAddress> dad_probes;
  uint64 next_rng = 0x1000000000000001ULL;
  TemporaryAddressManager mgr(
      kTestMac,
      [&](const IpAddress& target) { dad_probes.push_back(target); }, {},
      [&]() { return next_rng++; }, /*enabled=*/true);

  NdpPrefixInformation pio;
  pio.prefix = *IpAddress::Parse("2001:db8::");
  pio.prefix_length = 64;
  pio.autonomous = true;
  pio.valid_lifetime_seconds = 500000;
  pio.preferred_lifetime_seconds = 200000;

  const auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(100));
  mgr.OnPrefixInformation(pio, t0);
  ASSERT(1u, mgr.addresses().size());
  ASSERT(1u, dad_probes.size());

  const IpAddress first_addr = mgr.addresses()[0].address;
  EXPECT(AddressState::Tentative, mgr.addresses()[0].state);
  EXPECT(AddressOrigin::Temporary, mgr.addresses()[0].origin);
  EXPECT(t0 + std::chrono::seconds(kTempValidLifetimeSeconds),
         mgr.addresses()[0].valid_until);
  EXPECT(t0 + std::chrono::seconds(kTempPreferredLifetimeSeconds),
         mgr.addresses()[0].preferred_until);

  // Simulate a DAD collision on `first_addr`; manager must immediately retry
  // with a newly generated IID.
  NdpNeighborAdvertisement collision_na;
  collision_na.target = first_addr;
  mgr.OnNeighborAdvertisement(collision_na, t0 + std::chrono::milliseconds(200));
  ASSERT(1u, mgr.addresses().size());
  ASSERT(2u, dad_probes.size());
  const IpAddress second_addr = mgr.addresses()[0].address;
  EXPECT(false, first_addr == second_addr);

  // Advance 1s past the DAD probe so `second_addr` becomes Preferred.
  const auto t_dad_done = t0 + std::chrono::milliseconds(1200);
  mgr.OnTimer(t_dad_done);
  EXPECT(AddressState::Preferred, mgr.addresses()[0].state);
  ASSERT(true, mgr.PreferredTemporaryAddress(pio.prefix).has_value());
  EXPECT(second_addr, *mgr.PreferredTemporaryAddress(pio.prefix));

  // Advance to the regeneration window (5 seconds before second_addr's preferred_until).
  const auto t_collision = t0 + std::chrono::milliseconds(200);
  const auto t_regen =
      t_collision + std::chrono::seconds(kTempPreferredLifetimeSeconds -
                                         kTempRegenAdvanceSeconds);
  mgr.OnTimer(t_regen);
  ASSERT(2u, mgr.addresses().size());
  EXPECT(AddressState::Preferred, mgr.addresses()[0].state);
  EXPECT(AddressState::Tentative, mgr.addresses()[1].state);

  // Advance past the first address's preferred lifetime and the replacement's
  // DAD window: the old address is now Deprecated and the replacement is Preferred.
  const auto t_after_pref =
      t_collision + std::chrono::seconds(kTempPreferredLifetimeSeconds + 1);
  mgr.OnTimer(t_after_pref);
  EXPECT(AddressState::Deprecated, mgr.addresses()[0].state);
  EXPECT(AddressState::Preferred, mgr.addresses()[1].state);
  EXPECT(mgr.addresses()[1].address,
         *mgr.PreferredTemporaryAddress(pio.prefix));
}

}  // namespace
