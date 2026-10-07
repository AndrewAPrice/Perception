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
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "perception/network/ip_address.h"
#include "perception/registry.h"
#include "status.h"

namespace perception {

// Dual-stack connection establishment strategy.
enum class NetworkConnectStrategy : int64 {
  kHappyEyeballs = 0,
  kStrictRfc6724 = 1,
};

// Per-family address configuration mode for a network interface.
enum class InterfaceAddressMode : int64 {
  kAutomatic = 0,
  kStatic = 1,
  kDisabled = 2,
};

// Static IP address and prefix length entry.
struct StaticAddressConfig {
  network::IpAddress address;
  uint8 prefix_length = 0;

  bool operator==(const StaticAddressConfig& other) const = default;
};

// Static routing table entry.
struct StaticRouteEntry {
  network::IpAddress destination;
  uint8 prefix_length = 0;
  network::IpAddress next_hop;
  std::string interface_id;

  bool operator==(const StaticRouteEntry& other) const = default;
};

// Packet filter firewall rule entry.
struct FirewallRuleEntry {
  std::string direction;
  std::string action;
  std::string protocol;
  std::string source;
  std::string destination;
  std::string port;

  bool operator==(const FirewallRuleEntry& other) const = default;
};

// Manual IPsec policy and security association entry.
struct IpsecPolicyEntry {
  network::IpAddress peer_address;
  std::string mode;
  std::string protocol;
  uint32 spi = 0;
  std::string key_hex;

  bool operator==(const IpsecPolicyEntry& other) const = default;
};

// Read-only active address status entry published by the Network Manager.
struct InterfaceAddressStatus {
  network::IpAddress address;
  uint8 prefix_length = 0;
  std::string origin;
  std::string state;
  std::string expires;

  bool operator==(const InterfaceAddressStatus& other) const = default;
};

// Global network stack configuration stored in the Network Manager namespace.
struct GlobalNetworkSettings {
  NetworkConnectStrategy connect_strategy =
      NetworkConnectStrategy::kHappyEyeballs;
  bool prefer_ipv4 = false;
  bool enable_temporary_addresses = false;
  bool ipv4_forwarding = false;
  bool ipv6_forwarding = false;
  std::vector<StaticRouteEntry> static_routes;
  std::vector<FirewallRuleEntry> firewall_rules;
  std::vector<IpsecPolicyEntry> ipsec_policies;

  bool operator==(const GlobalNetworkSettings& other) const = default;
};

// Per-interface configuration stored under "interfaces/<interface_id>/...".
struct InterfaceNetworkSettings {
  InterfaceAddressMode ipv4_mode = InterfaceAddressMode::kAutomatic;
  std::vector<StaticAddressConfig> ipv4_static_addresses;
  std::optional<network::IpAddress> ipv4_gateway;
  std::vector<network::IpAddress> ipv4_dns_servers;

  InterfaceAddressMode ipv6_mode = InterfaceAddressMode::kAutomatic;
  std::vector<StaticAddressConfig> ipv6_static_addresses;
  std::optional<network::IpAddress> ipv6_gateway;
  std::vector<network::IpAddress> ipv6_dns_servers;
  bool ipv6_router_mode = false;

  bool operator==(const InterfaceNetworkSettings& other) const = default;
};

// RAII handle for a set of Registry listeners watching network settings.
class NetworkSettingsSubscription {
 public:
  NetworkSettingsSubscription() = default;
  explicit NetworkSettingsSubscription(
      std::vector<RegistryListenerToken> tokens);
  ~NetworkSettingsSubscription();

  NetworkSettingsSubscription(const NetworkSettingsSubscription&) = delete;
  NetworkSettingsSubscription& operator=(const NetworkSettingsSubscription&) =
      delete;

  NetworkSettingsSubscription(NetworkSettingsSubscription&& other) noexcept;
  NetworkSettingsSubscription& operator=(
      NetworkSettingsSubscription&& other) noexcept;

  // Unregisters all underlying Registry listeners.
  void Unsubscribe();

 private:
  std::vector<RegistryListenerToken> tokens_;
};

// Formats a 6-byte MAC address as a lowercase colon-separated identifier
// (e.g., "52:54:00:12:34:56").
std::string FormatInterfaceMacId(const std::array<uint8, 6>& mac);

// Builds a full per-interface Registry key ("interfaces/<id>/<setting_key>").
std::string MakeInterfaceSettingKey(std::string_view interface_id,
                                    std::string_view setting_key);

// Reads global network settings from the Registry, falling back to defaults.
GlobalNetworkSettings ReadGlobalNetworkSettings();

// Writes global network settings to the Registry atomically.
Status WriteGlobalNetworkSettings(const GlobalNetworkSettings& settings);

// Ensures a network interface's setting group exists in the Registry, writing
// default values for any missing keys so the Settings UI discovers the card.
Status EnsureInterfaceNetworkSettingsGroup(std::string_view interface_id);

// Reads per-interface network settings from the Registry, falling back to
// defaults for any unset keys.
InterfaceNetworkSettings ReadInterfaceNetworkSettings(
    std::string_view interface_id);

// Writes per-interface network settings to the Registry atomically.
Status WriteInterfaceNetworkSettings(std::string_view interface_id,
                                     const InterfaceNetworkSettings& settings);

// Publishes volatile per-interface status values (currentAddresses table,
// currentRouters string, and currentDnsServers string) to the Registry.
Status PublishInterfaceStatus(
    std::string_view interface_id,
    const std::vector<InterfaceAddressStatus>& addresses,
    const std::vector<network::IpAddress>& routers,
    const std::vector<network::IpAddress>& dns_servers);

// Reads the published currentAddresses table for an interface.
std::vector<InterfaceAddressStatus> ReadInterfaceCurrentAddresses(
    std::string_view interface_id);

// Reads the published currentRouters list for an interface.
std::vector<network::IpAddress> ReadInterfaceCurrentRouters(
    std::string_view interface_id);

// Reads the published currentDnsServers list for an interface.
std::vector<network::IpAddress> ReadInterfaceCurrentDnsServers(
    std::string_view interface_id);

// Subscribes to changes on all global network settings keys.
NetworkSettingsSubscription SubscribeGlobalNetworkSettings(
    std::function<void()> on_change);

// Subscribes to changes on all configurable settings keys for an interface.
NetworkSettingsSubscription SubscribeInterfaceNetworkSettings(
    std::string_view interface_id, std::function<void()> on_change);

}  // namespace perception
