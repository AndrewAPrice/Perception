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

#include "perception/network_settings.h"

#include <string>
#include <vector>

#include "testing.h"

using ::perception::EnsureInterfaceNetworkSettingsGroup;
using ::perception::FirewallRuleEntry;
using ::perception::FormatInterfaceMacId;
using ::perception::GlobalNetworkSettings;
using ::perception::InterfaceAddressMode;
using ::perception::InterfaceAddressStatus;
using ::perception::InterfaceNetworkSettings;
using ::perception::IpsecPolicyEntry;
using ::perception::MakeInterfaceSettingKey;
using ::perception::NetworkConnectStrategy;
using ::perception::PublishInterfaceStatus;
using ::perception::ReadGlobalNetworkSettings;
using ::perception::ReadInterfaceCurrentAddresses;
using ::perception::ReadInterfaceCurrentDnsServers;
using ::perception::ReadInterfaceCurrentRouters;
using ::perception::ReadInterfaceNetworkSettings;
using ::perception::StaticAddressConfig;
using ::perception::StaticRouteEntry;
using ::perception::SubscribeGlobalNetworkSettings;
using ::perception::SubscribeInterfaceNetworkSettings;
using ::perception::WriteGlobalNetworkSettings;
using ::perception::WriteInterfaceNetworkSettings;
using ::perception::network::IpAddress;

TEST(FormatInterfaceMacIdAndKey) {
  std::array<uint8, 6> mac = {0x52, 0x54, 0x00, 0x12, 0x34, 0xab};
  EXPECT(std::string("52:54:00:12:34:ab"), FormatInterfaceMacId(mac));
  EXPECT(std::string("interfaces/52:54:00:12:34:ab/ipv6Mode"),
         MakeInterfaceSettingKey("52:54:00:12:34:ab", "ipv6Mode"));
}

TEST(GlobalNetworkSettingsRoundTripAndSubscription) {
  int notifications = 0;
  auto sub = SubscribeGlobalNetworkSettings([&notifications]() {
    ++notifications;
  });

  GlobalNetworkSettings settings;
  settings.connect_strategy = NetworkConnectStrategy::kStrictRfc6724;
  settings.prefer_ipv4 = true;
  settings.enable_temporary_addresses = true;
  settings.ipv4_forwarding = true;
  settings.ipv6_forwarding = true;
  settings.static_routes.push_back(StaticRouteEntry{
      *IpAddress::Parse("2001:db8:1::"), 64, *IpAddress::Parse("fe80::1"),
      "52:54:00:12:34:56"});
  settings.firewall_rules.push_back(FirewallRuleEntry{
      "Inbound", "Drop", "TCP", "Any", "Any", "22"});
  settings.ipsec_policies.push_back(IpsecPolicyEntry{
      *IpAddress::Parse("2001:db8::2"), "Transport", "ESP", 1001,
      "0102030405060708"});

  EXPECT(Status::OK, WriteGlobalNetworkSettings(settings));
  EXPECT(true, notifications > 0);

  GlobalNetworkSettings loaded = ReadGlobalNetworkSettings();
  EXPECT(true, loaded == settings);

  int before_unsub = notifications;
  sub.Unsubscribe();
  settings.prefer_ipv4 = false;
  EXPECT(Status::OK, WriteGlobalNetworkSettings(settings));
  EXPECT(before_unsub, notifications);
}

TEST(InterfaceNetworkSettingsAndStatusRoundTrip) {
  constexpr std::string_view kIfId = "52:54:00:12:34:56";
  EXPECT(Status::OK, EnsureInterfaceNetworkSettingsGroup(kIfId));

  InterfaceNetworkSettings defaults = ReadInterfaceNetworkSettings(kIfId);
  EXPECT(true, defaults.ipv4_mode == InterfaceAddressMode::kAutomatic);
  EXPECT(true, defaults.ipv6_mode == InterfaceAddressMode::kAutomatic);
  EXPECT(false, defaults.ipv6_router_mode);

  int notifications = 0;
  auto sub = SubscribeInterfaceNetworkSettings(
      kIfId, [&notifications]() { ++notifications; });

  InterfaceNetworkSettings custom;
  custom.ipv4_mode = InterfaceAddressMode::kStatic;
  custom.ipv4_static_addresses.push_back(
      StaticAddressConfig{*IpAddress::Parse("10.0.2.15"), 24});
  custom.ipv4_gateway = *IpAddress::Parse("10.0.2.2");
  custom.ipv4_dns_servers = {*IpAddress::Parse("10.0.2.3"),
                             *IpAddress::Parse("8.8.8.8")};
  custom.ipv6_mode = InterfaceAddressMode::kStatic;
  custom.ipv6_static_addresses.push_back(
      StaticAddressConfig{*IpAddress::Parse("fd00::15"), 64});
  custom.ipv6_gateway = *IpAddress::Parse("fe80::2");
  custom.ipv6_dns_servers = {*IpAddress::Parse("2001:4860:4860::8888")};
  custom.ipv6_router_mode = true;

  EXPECT(Status::OK, WriteInterfaceNetworkSettings(kIfId, custom));
  EXPECT(true, notifications > 0);

  InterfaceNetworkSettings loaded = ReadInterfaceNetworkSettings(kIfId);
  EXPECT(true, loaded == custom);

  std::vector<InterfaceAddressStatus> statuses = {
      {*IpAddress::Parse("10.0.2.15"), 24, "Static", "Preferred", "Forever"},
      {*IpAddress::Parse("fe80::5054:ff:fe12:3456"), 64, "Link-Local",
       "Preferred", "Forever"},
      {*IpAddress::Parse("2001:db8::15"), 64, "SLAAC", "Preferred", "3600s"},
  };
  std::vector<IpAddress> routers = {*IpAddress::Parse("10.0.2.2"),
                                    *IpAddress::Parse("fe80::2")};
  std::vector<IpAddress> dns = {*IpAddress::Parse("10.0.2.3"),
                                *IpAddress::Parse("2001:4860:4860::8888")};

  EXPECT(Status::OK, PublishInterfaceStatus(kIfId, statuses, routers, dns));
  EXPECT(true, ReadInterfaceCurrentAddresses(kIfId) == statuses);
  EXPECT(true, ReadInterfaceCurrentRouters(kIfId) == routers);
  EXPECT(true, ReadInterfaceCurrentDnsServers(kIfId) == dns);
}
