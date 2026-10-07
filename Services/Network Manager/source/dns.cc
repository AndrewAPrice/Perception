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

#include "dns.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "address_selection.h"
#include "dns_cache.h"
#include "dns_message.h"
#include "interface.h"
#include "nat64_clat.h"
#include "perception/fibers.h"
#include "perception/time.h"
#include "socket.h"

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;
using ::perception::network::ResolveHostResponse;

namespace {

// Source port used when originating outbound DNS queries.
constexpr uint16 kDnsSourcePort = 50053;

// Destination port for DNS servers.
constexpr uint16 kDnsDestinationPort = 53;

// Timeout duration for individual DNS resolution attempts.
constexpr auto kDnsAttemptTimeout = std::chrono::milliseconds(1500);

// Grace period to wait for the second address family once one family succeeds.
constexpr auto kDnsSecondFamilyGracePeriod = std::chrono::milliseconds(50);

// Mask for per-transaction UDP source port offset.
constexpr uint16 kDnsSourcePortOffsetMask = 0x03FF;

// Fallback public IPv4 DNS server address (8.8.8.8).
const IpAddress kFallbackDnsServerIp = IpAddress::V4(8, 8, 8, 8);

// Default local slirp IPv4 DNS server address (10.0.2.3).
const IpAddress kDefaultSlirpDnsServerIp = IpAddress::V4(10, 0, 2, 3);

// Total retry attempts for DNS queries.
constexpr int kMaxDnsAttempts = 3;

struct PendingDnsTransaction {
  uint16 transaction_id = 0;
  uint16 source_port = kDnsSourcePort;
  std::string canonical_name;
  DnsRecordType type = DnsRecordType::A;
  bool completed = false;
  DnsResponse response;
  ::perception::Fiber* fiber = nullptr;
};

std::vector<std::shared_ptr<PendingDnsTransaction>> pending_dns_queries;
DnsCache dns_cache;
uint16 next_dns_id = 1;

std::vector<IpAddress> CollectDnsServers() {
  std::vector<IpAddress> servers = GetActiveDnsServers();
  if (servers.empty()) servers.push_back(kDefaultSlirpDnsServerIp);
  servers.push_back(kFallbackDnsServerIp);
  return servers;
}

std::vector<IpAddress> SortResolvedAddresses(
    const std::vector<IpAddress>& addresses) {
  AddressSelectionOptions opts = DefaultAddressSelectionOptions();
  opts.prefer_ipv4 = GetPreferIpv4Setting();
  AddressSelector selector(opts);

  return selector.SortDestinations(
      addresses, [](const IpAddress& dst) -> std::optional<SelectedSource> {
        auto iface_idx = SelectInterfaceForDestination(dst);
        if (!iface_idx.has_value()) return std::nullopt;
        auto src = SelectSourceAddress(*iface_idx, dst);
        if (!src.has_value()) return std::nullopt;
        SelectedSource selected;
        selected.address = *src;
        if (NetworkInterface* iface = GetInterface(*iface_idx);
            iface != nullptr) {
          for (const InterfaceAddress& entry : iface->addresses) {
            if (entry.address == *src) {
              selected.deprecated = (entry.state == AddressState::Deprecated);
              break;
            }
          }
        }
        return selected;
      });
}

}  // namespace

uint16 GetNextDnsId() { return next_dns_id++; }

StatusOr<ResolveHostResponse> PerformDnsResolution(const std::string& host,
                                                   IpAddressFamily family) {
  ResolveHostResponse response;
  if (auto parsed = IpAddress::Parse(host); parsed.has_value()) {
    if (family == IpAddressFamily::Unspecified || parsed->family() == family) {
      response.addresses.push_back(*parsed);
      return response;
    }
    return Status::INTERNAL_ERROR;
  }

  if (GetNetworkInterfaceCount() == 0) return Status::INTERNAL_ERROR;

  const std::string canon_host = CanonicalDnsName(host);
  if (canon_host.empty()) return Status::INVALID_ARGUMENT;

  const auto now = std::chrono::steady_clock::now();
  dns_cache.Purge(now);

  const bool want_v6 = (family == IpAddressFamily::Unspecified ||
                        family == IpAddressFamily::V6);
  const bool want_v4 = (family == IpAddressFamily::Unspecified ||
                        family == IpAddressFamily::V4 ||
                        GetActiveNat64Prefix().has_value());

  std::optional<DnsCache::Result> cached_v6 =
      want_v6 ? dns_cache.Lookup(canon_host, DnsRecordType::Aaaa, now)
              : std::nullopt;
  std::optional<DnsCache::Result> cached_v4 =
      want_v4 ? dns_cache.Lookup(canon_host, DnsRecordType::A, now)
              : std::nullopt;

  auto current_fiber = ::perception::GetCurrentlyExecutingFiber();
  std::vector<std::shared_ptr<PendingDnsTransaction>> active_txs;
  std::vector<std::string> encoded_packets;

  auto add_query_if_needed = [&](DnsRecordType type,
                                 const std::optional<DnsCache::Result>& cached) {
    if (cached.has_value()) return;
    const uint16 tx_id = GetNextDnsId();
    auto encoded = EncodeDnsQuery(tx_id, canon_host, type);
    if (!encoded.has_value()) return;
    auto tx = std::make_shared<PendingDnsTransaction>();
    tx->transaction_id = tx_id;
    tx->source_port = static_cast<uint16>(
        kDnsSourcePort + (tx_id & kDnsSourcePortOffsetMask));
    tx->canonical_name = canon_host;
    tx->type = type;
    tx->fiber = nullptr;
    pending_dns_queries.push_back(tx);
    active_txs.push_back(tx);
    encoded_packets.push_back(std::move(*encoded));
  };

  if (want_v6) add_query_if_needed(DnsRecordType::Aaaa, cached_v6);
  if (want_v4) add_query_if_needed(DnsRecordType::A, cached_v4);

  if (!active_txs.empty()) {
    const std::vector<IpAddress> servers = CollectDnsServers();
    for (int attempt = 0; attempt < kMaxDnsAttempts; ++attempt) {
      IpAddress server_ip =
          servers[std::min(static_cast<size_t>(attempt), servers.size() - 1)];
      size_t iface_idx = SelectInterfaceForDestination(server_ip).value_or(0);

      for (size_t i = 0; i < active_txs.size(); ++i) {
        if (!active_txs[i]->completed) {
          const uint16 src_port = active_txs[i]->source_port;
          if (SendUdpPacket(iface_idx, src_port, server_ip,
                            kDnsDestinationPort,
                            encoded_packets[i]) != Status::OK &&
              server_ip != kDefaultSlirpDnsServerIp) {
            server_ip = kDefaultSlirpDnsServerIp;
            iface_idx = SelectInterfaceForDestination(server_ip).value_or(0);
            (void)SendUdpPacket(iface_idx, src_port, server_ip,
                                kDnsDestinationPort, encoded_packets[i]);
          }
          if (attempt > 0 && !servers.empty() && server_ip != servers.front()) {
            const IpAddress primary_ip = servers.front();
            const size_t primary_iface =
                SelectInterfaceForDestination(primary_ip).value_or(0);
            (void)SendUdpPacket(primary_iface, src_port, primary_ip,
                                kDnsDestinationPort, encoded_packets[i]);
          }
        }
      }

      bool all_done = true;
      for (const auto& tx : active_txs) {
        if (!tx->completed) {
          all_done = false;
          break;
        }
      }
      if (all_done) break;

      for (auto& tx : active_txs) tx->fiber = current_fiber;

      struct AttemptState {
        bool finished = false;
        bool timed_out = false;
      };
      auto attempt_state = std::make_shared<AttemptState>();
      ::perception::AfterDuration(
          kDnsAttemptTimeout, [current_fiber, attempt_state]() {
            if (!attempt_state->finished) {
              attempt_state->finished = true;
              attempt_state->timed_out = true;
              if (current_fiber != nullptr) current_fiber->WakeUp();
            }
          });

      bool grace_timer_started = false;
      while (!attempt_state->timed_out) {
        all_done = true;
        bool any_positive = false;
        for (const auto& tx : active_txs) {
          if (!tx->completed) {
            all_done = false;
          } else if (!tx->response.addresses.empty()) {
            any_positive = true;
          }
        }
        if (all_done) break;
        if (any_positive && !grace_timer_started) {
          grace_timer_started = true;
          ::perception::AfterDuration(
              kDnsSecondFamilyGracePeriod, [current_fiber, attempt_state]() {
                if (!attempt_state->finished) {
                  attempt_state->finished = true;
                  attempt_state->timed_out = true;
                  if (current_fiber != nullptr) current_fiber->WakeUp();
                }
              });
        }
        ::perception::Sleep();
      }
      attempt_state->finished = true;
      for (auto& tx : active_txs) tx->fiber = nullptr;

      all_done = true;
      bool any_positive = false;
      for (const auto& tx : active_txs) {
        if (!tx->completed) {
          all_done = false;
        } else if (!tx->response.addresses.empty()) {
          any_positive = true;
        }
      }
      if (all_done || any_positive) break;
    }

    for (const auto& tx : active_txs) {
      tx->fiber = nullptr;
      pending_dns_queries.erase(
          std::remove(pending_dns_queries.begin(), pending_dns_queries.end(),
                      tx),
          pending_dns_queries.end());
    }
  }

  const auto after_now = std::chrono::steady_clock::now();
  if (want_v6 && !cached_v6.has_value())
    cached_v6 = dns_cache.Lookup(canon_host, DnsRecordType::Aaaa, after_now);
  if (want_v4 && !cached_v4.has_value())
    cached_v4 = dns_cache.Lookup(canon_host, DnsRecordType::A, after_now);

  std::vector<IpAddress> v6_addrs;
  std::vector<IpAddress> v4_addrs;
  if (cached_v6.has_value()) {
    for (const auto& rec : cached_v6->addresses)
      v6_addrs.push_back(rec.address);
  } else {
    for (const auto& tx : active_txs) {
      if (tx->completed && tx->type == DnsRecordType::Aaaa) {
        for (const auto& rec : tx->response.addresses)
          v6_addrs.push_back(rec.address);
      }
    }
  }
  if (cached_v4.has_value()) {
    for (const auto& rec : cached_v4->addresses)
      v4_addrs.push_back(rec.address);
  } else {
    for (const auto& tx : active_txs) {
      if (tx->completed && tx->type == DnsRecordType::A) {
        for (const auto& rec : tx->response.addresses)
          v4_addrs.push_back(rec.address);
      }
    }
  }

  if (want_v6 && v6_addrs.empty() && !v4_addrs.empty()) {
    if (auto nat64 = GetActiveNat64Prefix(); nat64.has_value()) {
      Nat64Prefix prefix{nat64->first, nat64->second, 0};
      for (const IpAddress& v4 : v4_addrs) {
        if (auto synth = SynthesizeIpv6FromIpv4(prefix, v4);
            synth.has_value()) {
          v6_addrs.push_back(*synth);
        }
      }
    }
  }

  std::vector<IpAddress> combined;
  if (family == IpAddressFamily::Unspecified || family == IpAddressFamily::V6) {
    combined.insert(combined.end(), v6_addrs.begin(), v6_addrs.end());
  }
  if (family == IpAddressFamily::Unspecified || family == IpAddressFamily::V4) {
    combined.insert(combined.end(), v4_addrs.begin(), v4_addrs.end());
  }

  if (combined.empty()) {
    std::cout << "DNS resolution failed for " << host << std::endl;
    return Status::INTERNAL_ERROR;
  }

  response.addresses = SortResolvedAddresses(combined);
  return response;
}

void ProcessDnsResponse(const uint8* payload, size_t len) {
  if (payload == nullptr || len == 0) return;
  std::string_view packet(reinterpret_cast<const char*>(payload), len);
  auto decoded = DecodeDnsResponse(packet);
  if (!decoded.has_value()) return;

  const auto now = std::chrono::steady_clock::now();
  const auto queries = pending_dns_queries;
  for (const auto& tx : queries) {
    if (tx->completed) continue;
    if (!IsResponseToQuery(*decoded, tx->transaction_id, tx->canonical_name,
                           tx->type)) {
      continue;
    }

    (void)dns_cache.Store(tx->canonical_name, tx->type, *decoded, now);
    tx->response = *decoded;
    tx->completed = true;

    if (tx->canonical_name == kIpv4OnlyArpaDomain &&
        tx->type == DnsRecordType::Aaaa) {
      std::vector<IpAddress> aaaa;
      for (const auto& rec : decoded->addresses) aaaa.push_back(rec.address);
      if (auto discovered = DiscoverNat64PrefixFromDns64Answers(aaaa);
          discovered.has_value()) {
        SetDiscoveredNat64Prefix(0, *discovered);
      }
    }

    ::perception::Fiber* fiber_to_wake = tx->fiber;
    if (fiber_to_wake != nullptr) {
      bool fiber_has_pending = false;
      for (const auto& other : queries) {
        if (other->fiber == fiber_to_wake && !other->completed) {
          fiber_has_pending = true;
          break;
        }
      }
      if (!fiber_has_pending || !decoded->addresses.empty()) {
        fiber_to_wake->WakeUp();
      }
    }
    break;
  }
}
