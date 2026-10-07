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

#include <charconv>
#include <cstdio>
#include <utility>

namespace perception {
namespace {

// Registry namespace used by the Network Manager service.
constexpr std::string_view kNetworkManagerNamespace = "Network Manager";

// Prefix for per-interface setting keys in the Network Manager namespace.
constexpr std::string_view kInterfacePrefix = "interfaces/";

// Global key for dual-stack connection strategy.
constexpr std::string_view kConnectStrategyKey = "connectStrategy";

// Global key for preferring IPv4 over IPv6.
constexpr std::string_view kPreferIpv4Key = "preferIpv4";

// Global key for enabling RFC 4941 IPv6 temporary privacy addresses.
constexpr std::string_view kEnableTemporaryAddressesKey =
    "enableTemporaryAddresses";

// Global key for IPv4 packet forwarding.
constexpr std::string_view kIpv4ForwardingKey = "ipv4Forwarding";

// Global key for IPv6 packet forwarding.
constexpr std::string_view kIpv6ForwardingKey = "ipv6Forwarding";

// Global key for static route table entries.
constexpr std::string_view kStaticRoutesKey = "staticRoutes";

// Global key for firewall rule table entries.
constexpr std::string_view kFirewallRulesKey = "firewallRules";

// Global key for IPsec policy table entries.
constexpr std::string_view kIpsecPoliciesKey = "ipsecPolicies";

// Per-interface key for IPv4 configuration mode.
constexpr std::string_view kIpv4ModeKey = "ipv4Mode";

// Per-interface key for static IPv4 addresses.
constexpr std::string_view kIpv4StaticAddressesKey = "ipv4StaticAddresses";

// Per-interface key for static IPv4 default gateway.
constexpr std::string_view kIpv4GatewayKey = "ipv4Gateway";

// Per-interface key for static IPv4 DNS servers.
constexpr std::string_view kIpv4DnsServersKey = "ipv4DnsServers";

// Per-interface key for IPv6 configuration mode.
constexpr std::string_view kIpv6ModeKey = "ipv6Mode";

// Per-interface key for static IPv6 addresses.
constexpr std::string_view kIpv6StaticAddressesKey = "ipv6StaticAddresses";

// Per-interface key for static IPv6 default gateway.
constexpr std::string_view kIpv6GatewayKey = "ipv6Gateway";

// Per-interface key for static IPv6 DNS servers.
constexpr std::string_view kIpv6DnsServersKey = "ipv6DnsServers";

// Per-interface key for sending IPv6 Router Advertisements.
constexpr std::string_view kIpv6RouterModeKey = "ipv6RouterMode";

// Per-interface read-only key for active IP addresses.
constexpr std::string_view kCurrentAddressesKey = "currentAddresses";

// Per-interface read-only key for active default routers.
constexpr std::string_view kCurrentRoutersKey = "currentRouters";

// Per-interface read-only key for active DNS servers.
constexpr std::string_view kCurrentDnsServersKey = "currentDnsServers";

using ::perception::network::IpAddress;
using ::perception::serialization::Value;

uint8 ParsePrefixLength(const Value& val) {
  if (auto i = val.IntegerValue()) {
    if (*i >= 0 && *i <= 128) return static_cast<uint8>(*i);
  }
  if (auto s = val.StringValue()) {
    int parsed = 0;
    auto [ptr, ec] =
        std::from_chars(s->data(), s->data() + s->size(), parsed);
    if (ec == std::errc() && parsed >= 0 && parsed <= 128)
      return static_cast<uint8>(parsed);
  }
  return 0;
}

uint32 ParseUint32(const Value& val) {
  if (auto i = val.IntegerValue()) {
    if (*i >= 0) return static_cast<uint32>(*i);
  }
  if (auto s = val.StringValue()) {
    uint32 parsed = 0;
    auto [ptr, ec] =
        std::from_chars(s->data(), s->data() + s->size(), parsed);
    if (ec == std::errc()) return parsed;
  }
  return 0;
}

std::string FormatIpAddressList(const std::vector<IpAddress>& addresses) {
  std::string out;
  for (const auto& addr : addresses) {
    if (addr.IsUnspecified()) continue;
    if (!out.empty()) out += ", ";
    out += addr.ToString();
  }
  return out;
}

std::vector<IpAddress> ParseIpAddressList(std::string_view text) {
  std::vector<IpAddress> result;
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() &&
           (text[i] == ' ' || text[i] == ',' || text[i] == ';' ||
            text[i] == '\t' || text[i] == '\n'))
      ++i;
    if (i >= text.size()) break;
    size_t start = i;
    while (i < text.size() && text[i] != ' ' && text[i] != ',' &&
           text[i] != ';' && text[i] != '\t' && text[i] != '\n')
      ++i;
    if (auto parsed = IpAddress::Parse(text.substr(start, i - start)))
      result.push_back(*parsed);
  }
  return result;
}

std::vector<StaticAddressConfig> ParseStaticAddressTable(const Value& val) {
  std::vector<StaticAddressConfig> out;
  const auto* rows = val.ArrayValue();
  if (!rows) return out;
  for (const auto& row_val : *rows) {
    const auto* cells = row_val.ArrayValue();
    if (!cells || cells->size() < 2) continue;
    auto addr_str = (*cells)[0].StringValue();
    if (!addr_str) continue;
    auto parsed_ip = IpAddress::Parse(*addr_str);
    if (!parsed_ip) continue;
    StaticAddressConfig cfg;
    cfg.address = *parsed_ip;
    cfg.prefix_length = ParsePrefixLength((*cells)[1]);
    out.push_back(cfg);
  }
  return out;
}

Value SerializeStaticAddressTable(
    const std::vector<StaticAddressConfig>& addresses) {
  std::vector<Value> rows;
  rows.reserve(addresses.size());
  for (const auto& entry : addresses) {
    std::vector<Value> cells;
    cells.emplace_back(entry.address.ToString());
    cells.emplace_back(static_cast<int64>(entry.prefix_length));
    rows.emplace_back(std::move(cells));
  }
  return Value(std::move(rows));
}

InterfaceAddressMode ParseInterfaceMode(const Value& val) {
  if (auto i = val.IntegerValue()) {
    switch (*i) {
      case 1:
        return InterfaceAddressMode::kStatic;
      case 2:
        return InterfaceAddressMode::kDisabled;
      default:
        return InterfaceAddressMode::kAutomatic;
    }
  }
  if (auto s = val.StringValue()) {
    if (*s == "Static") return InterfaceAddressMode::kStatic;
    if (*s == "Disabled") return InterfaceAddressMode::kDisabled;
  }
  return InterfaceAddressMode::kAutomatic;
}

}  // namespace

NetworkSettingsSubscription::NetworkSettingsSubscription(
    std::vector<RegistryListenerToken> tokens)
    : tokens_(std::move(tokens)) {}

NetworkSettingsSubscription::~NetworkSettingsSubscription() { Unsubscribe(); }

NetworkSettingsSubscription::NetworkSettingsSubscription(
    NetworkSettingsSubscription&& other) noexcept
    : tokens_(std::move(other.tokens_)) {
  other.tokens_.clear();
}

NetworkSettingsSubscription& NetworkSettingsSubscription::operator=(
    NetworkSettingsSubscription&& other) noexcept {
  if (this != &other) {
    Unsubscribe();
    tokens_ = std::move(other.tokens_);
    other.tokens_.clear();
  }
  return *this;
}

void NetworkSettingsSubscription::Unsubscribe() {
  for (RegistryListenerToken token : tokens_)
    (void)UnregisterRegistryListener(token);
  tokens_.clear();
}

std::string FormatInterfaceMacId(const std::array<uint8, 6>& mac) {
  char buf[18];
  std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0],
                mac[1], mac[2], mac[3], mac[4], mac[5]);
  return std::string(buf);
}

std::string MakeInterfaceSettingKey(std::string_view interface_id,
                                    std::string_view setting_key) {
  std::string key;
  key.reserve(kInterfacePrefix.size() + interface_id.size() + 1 +
              setting_key.size());
  key.append(kInterfacePrefix);
  key.append(interface_id);
  key.push_back('/');
  key.append(setting_key);
  return key;
}

GlobalNetworkSettings ReadGlobalNetworkSettings() {
  GlobalNetworkSettings settings;

  if (auto val_or = GetRegistryValue(RegistryCorpus::APPLICATIONS,
                                     kNetworkManagerNamespace,
                                     kConnectStrategyKey);
      val_or.Ok()) {
    if (auto i = val_or->IntegerValue()) {
      settings.connect_strategy = (*i == 1)
                                      ? NetworkConnectStrategy::kStrictRfc6724
                                      : NetworkConnectStrategy::kHappyEyeballs;
    } else if (auto s = val_or->StringValue()) {
      if (*s == "Strict RFC 6724")
        settings.connect_strategy = NetworkConnectStrategy::kStrictRfc6724;
    }
  }

  if (auto val_or = GetRegistryValue(RegistryCorpus::APPLICATIONS,
                                     kNetworkManagerNamespace, kPreferIpv4Key);
      val_or.Ok()) {
    settings.prefer_ipv4 = val_or->BoolValue().value_or(false);
  }

  if (auto val_or = GetRegistryValue(RegistryCorpus::APPLICATIONS,
                                     kNetworkManagerNamespace,
                                     kEnableTemporaryAddressesKey);
      val_or.Ok()) {
    settings.enable_temporary_addresses =
        val_or->BoolValue().value_or(false);
  }

  if (auto val_or = GetRegistryValue(RegistryCorpus::APPLICATIONS,
                                     kNetworkManagerNamespace,
                                     kIpv4ForwardingKey);
      val_or.Ok()) {
    settings.ipv4_forwarding = val_or->BoolValue().value_or(false);
  }

  if (auto val_or = GetRegistryValue(RegistryCorpus::APPLICATIONS,
                                     kNetworkManagerNamespace,
                                     kIpv6ForwardingKey);
      val_or.Ok()) {
    settings.ipv6_forwarding = val_or->BoolValue().value_or(false);
  }

  if (auto val_or = GetRegistryValue(RegistryCorpus::APPLICATIONS,
                                     kNetworkManagerNamespace,
                                     kStaticRoutesKey);
      val_or.Ok()) {
    if (const auto* rows = val_or->ArrayValue()) {
      for (const auto& row_val : *rows) {
        const auto* cells = row_val.ArrayValue();
        if (!cells || cells->size() < 4) continue;
        auto dest_str = (*cells)[0].StringValue();
        auto next_hop_str = (*cells)[2].StringValue();
        if (!dest_str || !next_hop_str) continue;
        auto dest_ip = IpAddress::Parse(*dest_str);
        auto next_hop_ip = IpAddress::Parse(*next_hop_str);
        if (!dest_ip || !next_hop_ip) continue;

        StaticRouteEntry route;
        route.destination = *dest_ip;
        route.prefix_length = ParsePrefixLength((*cells)[1]);
        route.next_hop = *next_hop_ip;
        route.interface_id =
            std::string((*cells)[3].StringValue().value_or(""));
        settings.static_routes.push_back(std::move(route));
      }
    }
  }

  if (auto val_or = GetRegistryValue(RegistryCorpus::APPLICATIONS,
                                     kNetworkManagerNamespace,
                                     kFirewallRulesKey);
      val_or.Ok()) {
    if (const auto* rows = val_or->ArrayValue()) {
      for (const auto& row_val : *rows) {
        const auto* cells = row_val.ArrayValue();
        if (!cells || cells->size() < 6) continue;
        FirewallRuleEntry rule;
        rule.direction = std::string((*cells)[0].StringValue().value_or(""));
        rule.action = std::string((*cells)[1].StringValue().value_or(""));
        rule.protocol = std::string((*cells)[2].StringValue().value_or(""));
        rule.source = std::string((*cells)[3].StringValue().value_or(""));
        rule.destination = std::string((*cells)[4].StringValue().value_or(""));
        rule.port = std::string((*cells)[5].StringValue().value_or(""));
        settings.firewall_rules.push_back(std::move(rule));
      }
    }
  }

  if (auto val_or = GetRegistryValue(RegistryCorpus::APPLICATIONS,
                                     kNetworkManagerNamespace,
                                     kIpsecPoliciesKey);
      val_or.Ok()) {
    if (const auto* rows = val_or->ArrayValue()) {
      for (const auto& row_val : *rows) {
        const auto* cells = row_val.ArrayValue();
        if (!cells || cells->size() < 5) continue;
        auto peer_str = (*cells)[0].StringValue();
        if (!peer_str) continue;
        auto peer_ip = IpAddress::Parse(*peer_str);
        if (!peer_ip) continue;

        IpsecPolicyEntry policy;
        policy.peer_address = *peer_ip;
        policy.mode = std::string((*cells)[1].StringValue().value_or(""));
        policy.protocol = std::string((*cells)[2].StringValue().value_or(""));
        policy.spi = ParseUint32((*cells)[3]);
        policy.key_hex = std::string((*cells)[4].StringValue().value_or(""));
        settings.ipsec_policies.push_back(std::move(policy));
      }
    }
  }

  return settings;
}

Status WriteGlobalNetworkSettings(const GlobalNetworkSettings& settings) {
  std::vector<RegistryKeyValue> batch;
  batch.push_back({std::string(kConnectStrategyKey),
                   Value(static_cast<int64>(settings.connect_strategy))});
  batch.push_back(
      {std::string(kPreferIpv4Key), Value(settings.prefer_ipv4)});
  batch.push_back({std::string(kEnableTemporaryAddressesKey),
                   Value(settings.enable_temporary_addresses)});
  batch.push_back(
      {std::string(kIpv4ForwardingKey), Value(settings.ipv4_forwarding)});
  batch.push_back(
      {std::string(kIpv6ForwardingKey), Value(settings.ipv6_forwarding)});

  std::vector<Value> route_rows;
  for (const auto& route : settings.static_routes) {
    std::vector<Value> cells;
    cells.emplace_back(route.destination.ToString());
    cells.emplace_back(static_cast<int64>(route.prefix_length));
    cells.emplace_back(route.next_hop.ToString());
    cells.emplace_back(route.interface_id);
    route_rows.emplace_back(std::move(cells));
  }
  batch.push_back(
      {std::string(kStaticRoutesKey), Value(std::move(route_rows))});

  std::vector<Value> fw_rows;
  for (const auto& rule : settings.firewall_rules) {
    std::vector<Value> cells;
    cells.emplace_back(rule.direction);
    cells.emplace_back(rule.action);
    cells.emplace_back(rule.protocol);
    cells.emplace_back(rule.source);
    cells.emplace_back(rule.destination);
    cells.emplace_back(rule.port);
    fw_rows.emplace_back(std::move(cells));
  }
  batch.push_back({std::string(kFirewallRulesKey), Value(std::move(fw_rows))});

  std::vector<Value> ipsec_rows;
  for (const auto& policy : settings.ipsec_policies) {
    std::vector<Value> cells;
    cells.emplace_back(policy.peer_address.ToString());
    cells.emplace_back(policy.mode);
    cells.emplace_back(policy.protocol);
    cells.emplace_back(static_cast<int64>(policy.spi));
    cells.emplace_back(policy.key_hex);
    ipsec_rows.emplace_back(std::move(cells));
  }
  batch.push_back(
      {std::string(kIpsecPoliciesKey), Value(std::move(ipsec_rows))});

  return SetRegistryValues(RegistryCorpus::APPLICATIONS,
                           kNetworkManagerNamespace, std::move(batch));
}

Status EnsureInterfaceNetworkSettingsGroup(std::string_view interface_id) {
  struct DefaultEntry {
    std::string_view suffix;
    Value default_value;
  };
  const std::vector<DefaultEntry> defaults = {
      {kIpv4ModeKey, Value(static_cast<int64>(0))},
      {kIpv4StaticAddressesKey, Value(std::vector<Value>{})},
      {kIpv4GatewayKey, Value(std::string(""))},
      {kIpv4DnsServersKey, Value(std::string(""))},
      {kIpv6ModeKey, Value(static_cast<int64>(0))},
      {kIpv6StaticAddressesKey, Value(std::vector<Value>{})},
      {kIpv6GatewayKey, Value(std::string(""))},
      {kIpv6DnsServersKey, Value(std::string(""))},
      {kIpv6RouterModeKey, Value(false)},
      {kCurrentAddressesKey, Value(std::vector<Value>{})},
      {kCurrentRoutersKey, Value(std::string(""))},
      {kCurrentDnsServersKey, Value(std::string(""))},
  };

  std::vector<RegistryKeyValue> missing;
  for (const auto& entry : defaults) {
    std::string full_key = MakeInterfaceSettingKey(interface_id, entry.suffix);
    auto existing = GetRegistryValue(RegistryCorpus::APPLICATIONS,
                                     kNetworkManagerNamespace, full_key);
    if (!existing.Ok())
      missing.push_back({std::move(full_key), entry.default_value});
  }
  if (missing.empty()) return Status::OK;
  return SetRegistryValues(RegistryCorpus::APPLICATIONS,
                           kNetworkManagerNamespace, std::move(missing));
}

InterfaceNetworkSettings ReadInterfaceNetworkSettings(
    std::string_view interface_id) {
  InterfaceNetworkSettings settings;

  if (auto val_or = GetRegistryValue(
          RegistryCorpus::APPLICATIONS, kNetworkManagerNamespace,
          MakeInterfaceSettingKey(interface_id, kIpv4ModeKey));
      val_or.Ok()) {
    settings.ipv4_mode = ParseInterfaceMode(*val_or);
  }

  if (auto val_or = GetRegistryValue(
          RegistryCorpus::APPLICATIONS, kNetworkManagerNamespace,
          MakeInterfaceSettingKey(interface_id, kIpv4StaticAddressesKey));
      val_or.Ok()) {
    settings.ipv4_static_addresses = ParseStaticAddressTable(*val_or);
  }

  if (auto val_or = GetRegistryValue(
          RegistryCorpus::APPLICATIONS, kNetworkManagerNamespace,
          MakeInterfaceSettingKey(interface_id, kIpv4GatewayKey));
      val_or.Ok()) {
    if (auto s = val_or->StringValue(); s && !s->empty()) {
      if (auto ip = IpAddress::Parse(*s); ip && ip->IsV4())
        settings.ipv4_gateway = *ip;
    }
  }

  if (auto val_or = GetRegistryValue(
          RegistryCorpus::APPLICATIONS, kNetworkManagerNamespace,
          MakeInterfaceSettingKey(interface_id, kIpv4DnsServersKey));
      val_or.Ok()) {
    if (auto s = val_or->StringValue()) {
      for (const auto& ip : ParseIpAddressList(*s)) {
        if (ip.IsV4()) settings.ipv4_dns_servers.push_back(ip);
      }
    }
  }

  if (auto val_or = GetRegistryValue(
          RegistryCorpus::APPLICATIONS, kNetworkManagerNamespace,
          MakeInterfaceSettingKey(interface_id, kIpv6ModeKey));
      val_or.Ok()) {
    settings.ipv6_mode = ParseInterfaceMode(*val_or);
  }

  if (auto val_or = GetRegistryValue(
          RegistryCorpus::APPLICATIONS, kNetworkManagerNamespace,
          MakeInterfaceSettingKey(interface_id, kIpv6StaticAddressesKey));
      val_or.Ok()) {
    settings.ipv6_static_addresses = ParseStaticAddressTable(*val_or);
  }

  if (auto val_or = GetRegistryValue(
          RegistryCorpus::APPLICATIONS, kNetworkManagerNamespace,
          MakeInterfaceSettingKey(interface_id, kIpv6GatewayKey));
      val_or.Ok()) {
    if (auto s = val_or->StringValue(); s && !s->empty()) {
      if (auto ip = IpAddress::Parse(*s); ip && ip->IsV6())
        settings.ipv6_gateway = *ip;
    }
  }

  if (auto val_or = GetRegistryValue(
          RegistryCorpus::APPLICATIONS, kNetworkManagerNamespace,
          MakeInterfaceSettingKey(interface_id, kIpv6DnsServersKey));
      val_or.Ok()) {
    if (auto s = val_or->StringValue()) {
      for (const auto& ip : ParseIpAddressList(*s)) {
        if (ip.IsV6()) settings.ipv6_dns_servers.push_back(ip);
      }
    }
  }

  if (auto val_or = GetRegistryValue(
          RegistryCorpus::APPLICATIONS, kNetworkManagerNamespace,
          MakeInterfaceSettingKey(interface_id, kIpv6RouterModeKey));
      val_or.Ok()) {
    settings.ipv6_router_mode = val_or->BoolValue().value_or(false);
  }

  return settings;
}

Status WriteInterfaceNetworkSettings(std::string_view interface_id,
                                     const InterfaceNetworkSettings& settings) {
  std::vector<RegistryKeyValue> batch;
  batch.push_back({MakeInterfaceSettingKey(interface_id, kIpv4ModeKey),
                   Value(static_cast<int64>(settings.ipv4_mode))});
  batch.push_back(
      {MakeInterfaceSettingKey(interface_id, kIpv4StaticAddressesKey),
       SerializeStaticAddressTable(settings.ipv4_static_addresses)});
  batch.push_back({MakeInterfaceSettingKey(interface_id, kIpv4GatewayKey),
                   Value(settings.ipv4_gateway
                             ? settings.ipv4_gateway->ToString()
                             : std::string(""))});
  batch.push_back({MakeInterfaceSettingKey(interface_id, kIpv4DnsServersKey),
                   Value(FormatIpAddressList(settings.ipv4_dns_servers))});
  batch.push_back({MakeInterfaceSettingKey(interface_id, kIpv6ModeKey),
                   Value(static_cast<int64>(settings.ipv6_mode))});
  batch.push_back(
      {MakeInterfaceSettingKey(interface_id, kIpv6StaticAddressesKey),
       SerializeStaticAddressTable(settings.ipv6_static_addresses)});
  batch.push_back({MakeInterfaceSettingKey(interface_id, kIpv6GatewayKey),
                   Value(settings.ipv6_gateway
                             ? settings.ipv6_gateway->ToString()
                             : std::string(""))});
  batch.push_back({MakeInterfaceSettingKey(interface_id, kIpv6DnsServersKey),
                   Value(FormatIpAddressList(settings.ipv6_dns_servers))});
  batch.push_back({MakeInterfaceSettingKey(interface_id, kIpv6RouterModeKey),
                   Value(settings.ipv6_router_mode)});

  return SetRegistryValues(RegistryCorpus::APPLICATIONS,
                           kNetworkManagerNamespace, std::move(batch));
}

Status PublishInterfaceStatus(
    std::string_view interface_id,
    const std::vector<InterfaceAddressStatus>& addresses,
    const std::vector<IpAddress>& routers,
    const std::vector<IpAddress>& dns_servers) {
  std::vector<Value> addr_rows;
  addr_rows.reserve(addresses.size());
  for (const auto& entry : addresses) {
    std::vector<Value> cells;
    cells.emplace_back(entry.address.ToString());
    cells.emplace_back(static_cast<int64>(entry.prefix_length));
    cells.emplace_back(entry.origin);
    cells.emplace_back(entry.state);
    cells.emplace_back(entry.expires);
    addr_rows.emplace_back(std::move(cells));
  }

  std::vector<RegistryKeyValue> batch;
  batch.push_back({MakeInterfaceSettingKey(interface_id, kCurrentAddressesKey),
                   Value(std::move(addr_rows))});
  batch.push_back({MakeInterfaceSettingKey(interface_id, kCurrentRoutersKey),
                   Value(FormatIpAddressList(routers))});
  batch.push_back({MakeInterfaceSettingKey(interface_id, kCurrentDnsServersKey),
                   Value(FormatIpAddressList(dns_servers))});

  return SetRegistryValues(RegistryCorpus::APPLICATIONS,
                           kNetworkManagerNamespace, std::move(batch));
}

std::vector<InterfaceAddressStatus> ReadInterfaceCurrentAddresses(
    std::string_view interface_id) {
  std::vector<InterfaceAddressStatus> out;
  auto val_or = GetRegistryValue(
      RegistryCorpus::APPLICATIONS, kNetworkManagerNamespace,
      MakeInterfaceSettingKey(interface_id, kCurrentAddressesKey));
  if (!val_or.Ok()) return out;
  const auto* rows = val_or->ArrayValue();
  if (!rows) return out;
  for (const auto& row_val : *rows) {
    const auto* cells = row_val.ArrayValue();
    if (!cells || cells->size() < 5) continue;
    auto addr_str = (*cells)[0].StringValue();
    if (!addr_str) continue;
    auto parsed_ip = IpAddress::Parse(*addr_str);
    if (!parsed_ip) continue;

    InterfaceAddressStatus status;
    status.address = *parsed_ip;
    status.prefix_length = ParsePrefixLength((*cells)[1]);
    status.origin = std::string((*cells)[2].StringValue().value_or(""));
    status.state = std::string((*cells)[3].StringValue().value_or(""));
    status.expires = std::string((*cells)[4].StringValue().value_or(""));
    out.push_back(std::move(status));
  }
  return out;
}

std::vector<IpAddress> ReadInterfaceCurrentRouters(
    std::string_view interface_id) {
  auto val_or = GetRegistryValue(
      RegistryCorpus::APPLICATIONS, kNetworkManagerNamespace,
      MakeInterfaceSettingKey(interface_id, kCurrentRoutersKey));
  if (!val_or.Ok()) return {};
  return ParseIpAddressList(val_or->StringValue().value_or(""));
}

std::vector<IpAddress> ReadInterfaceCurrentDnsServers(
    std::string_view interface_id) {
  auto val_or = GetRegistryValue(
      RegistryCorpus::APPLICATIONS, kNetworkManagerNamespace,
      MakeInterfaceSettingKey(interface_id, kCurrentDnsServersKey));
  if (!val_or.Ok()) return {};
  return ParseIpAddressList(val_or->StringValue().value_or(""));
}

NetworkSettingsSubscription SubscribeGlobalNetworkSettings(
    std::function<void()> on_change) {
  const std::array<std::string_view, 8> keys = {
      kConnectStrategyKey,          kPreferIpv4Key,
      kEnableTemporaryAddressesKey, kIpv4ForwardingKey,
      kIpv6ForwardingKey,           kStaticRoutesKey,
      kFirewallRulesKey,            kIpsecPoliciesKey,
  };
  std::vector<RegistryListenerToken> tokens;
  for (std::string_view key : keys) {
    if (auto token_or = RegisterRegistryListener(
            RegistryCorpus::APPLICATIONS, kNetworkManagerNamespace, key,
            on_change);
        token_or.Ok())
      tokens.push_back(*token_or);
  }
  return NetworkSettingsSubscription(std::move(tokens));
}

NetworkSettingsSubscription SubscribeInterfaceNetworkSettings(
    std::string_view interface_id, std::function<void()> on_change) {
  const std::array<std::string_view, 9> suffixes = {
      kIpv4ModeKey,            kIpv4StaticAddressesKey, kIpv4GatewayKey,
      kIpv4DnsServersKey,      kIpv6ModeKey,            kIpv6StaticAddressesKey,
      kIpv6GatewayKey,         kIpv6DnsServersKey,      kIpv6RouterModeKey,
  };
  std::vector<RegistryListenerToken> tokens;
  for (std::string_view suffix : suffixes) {
    std::string full_key = MakeInterfaceSettingKey(interface_id, suffix);
    if (auto token_or = RegisterRegistryListener(
            RegistryCorpus::APPLICATIONS, kNetworkManagerNamespace, full_key,
            on_change);
        token_or.Ok())
      tokens.push_back(*token_or);
  }
  return NetworkSettingsSubscription(std::move(tokens));
}

}  // namespace perception
