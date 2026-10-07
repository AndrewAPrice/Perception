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

#include "ndp.h"

#include <algorithm>
#include <functional>

#include "icmpv6.h"
#include "wire_format.h"

using ::perception::network::IpAddress;

namespace {

// Unit size in bytes of the NDP option Length field.
constexpr size_t kOptionUnitBytes = 8;

// Body size of a 6-byte Ethernet link-layer address option (8 - 2).
constexpr size_t kEthernetLinkLayerBodySize = 6;

// Body size of a Prefix Information Option (32 - 2).
constexpr size_t kPrefixInfoBodySize = 30;

// Body size of an MTU Option (8 - 2).
constexpr size_t kMtuBodySize = 6;

// Minimum body size of an RDNSS Option (24 - 2).
constexpr size_t kMinRdnssBodySize = 22;

// Minimum body size of a DNSSL Option (16 - 2).
constexpr size_t kMinDnsslBodySize = 14;

// Body size of a PREF64 Option (16 - 2).
constexpr size_t kPref64BodySize = 14;

// Number of prefix bytes carried inside a PREF64 Option.
constexpr size_t kPref64PrefixBytes = 12;

// Maximum valid IPv6 prefix length.
constexpr uint8 kMaxIpv6PrefixLength = 128;

// PIO flag bit for On-Link (L).
constexpr uint8 kPioFlagOnLink = 0x80;

// PIO flag bit for Autonomous Address Configuration (A).
constexpr uint8 kPioFlagAutonomous = 0x40;

// RA flag bit for Managed Address Configuration (M).
constexpr uint8 kRaFlagManaged = 0x80;

// RA flag bit for Other Configuration (O).
constexpr uint8 kRaFlagOtherConfig = 0x40;

// NA flag bit for Router (R).
constexpr uint32 kNaFlagRouter = 0x80000000u;

// NA flag bit for Solicited (S).
constexpr uint32 kNaFlagSolicited = 0x40000000u;

// NA flag bit for Override (O).
constexpr uint32 kNaFlagOverride = 0x20000000u;

// Maximum scaled lifetime value in PREF64 (13 bits * 8 = 65528 seconds).
constexpr uint32 kMaxPref64LifetimeSeconds = 65528;

// Validates common NDP requirements: hop limit 255, matching type, code 0.
bool ValidateCommonNdpHeader(const Ipv6Header& ipv6_header,
                             Icmpv6Type expected_type, uint8 type, uint8 code,
                             bool reader_ok) {
  if (!reader_ok) return false;
  if (ipv6_header.hop_limit != kNdpHopLimit) return false;
  if (type != static_cast<uint8>(expected_type) || code != 0) return false;
  return true;
}

// Walks TLV options in `options_bytes`. Returns false if any option has length
// 0, overruns the buffer, or is rejected by `handler`.
bool WalkNdpOptions(
    std::string_view options_bytes,
    const std::function<bool(NdpOptionType, std::string_view)>& handler) {
  WireReader reader(options_bytes);
  while (reader.Remaining() > 0) {
    uint8 type = reader.ReadU8();
    uint8 length_units = reader.ReadU8();
    if (!reader.ok() || length_units == 0) return false;
    size_t body_length = static_cast<size_t>(length_units) * kOptionUnitBytes - 2;
    std::string_view body = reader.ReadBytes(body_length);
    if (!reader.ok()) return false;
    if (!handler(static_cast<NdpOptionType>(type), body)) return false;
  }
  return true;
}

std::optional<HardwareAddress> ParseHardwareAddressOption(
    std::string_view body) {
  if (body.size() != kEthernetLinkLayerBodySize) return std::nullopt;
  HardwareAddress mac{};
  for (size_t i = 0; i < mac.size(); i++)
    mac[i] = static_cast<uint8>(body[i]);
  return mac;
}

void AppendHardwareAddressOption(NdpOptionType type,
                                 const HardwareAddress& mac,
                                 WireWriter& writer) {
  writer.WriteU8(static_cast<uint8>(type));
  writer.WriteU8(1);
  for (uint8 byte : mac) writer.WriteU8(byte);
}

std::optional<uint8> DecodePref64Plc(uint8 plc) {
  switch (plc) {
    case 0:
      return 96;
    case 1:
      return 64;
    case 2:
      return 56;
    case 3:
      return 48;
    case 4:
      return 40;
    case 5:
      return 32;
    default:
      return std::nullopt;
  }
}

std::optional<uint8> EncodePref64Plc(uint8 prefix_length) {
  switch (prefix_length) {
    case 96:
      return 0;
    case 64:
      return 1;
    case 56:
      return 2;
    case 48:
      return 3;
    case 40:
      return 4;
    case 32:
      return 5;
    default:
      return std::nullopt;
  }
}

bool DecodeDnsslDomains(std::string_view encoded,
                        std::vector<std::string>& out_domains) {
  size_t pos = 0;
  while (pos < encoded.size()) {
    if (encoded[pos] == '\0') {
      pos++;
      continue;
    }
    std::string domain;
    while (true) {
      if (pos >= encoded.size()) return false;
      uint8 label_len = static_cast<uint8>(encoded[pos++]);
      if (label_len == 0) break;
      if ((label_len & 0xC0) != 0 || pos + label_len > encoded.size())
        return false;
      if (!domain.empty()) domain.push_back('.');
      domain.append(encoded.substr(pos, label_len));
      pos += label_len;
    }
    if (!domain.empty()) out_domains.push_back(std::move(domain));
  }
  return true;
}

void AppendDnsslOption(const NdpDnsslOption& dnssl, WireWriter& writer) {
  std::string names;
  for (const std::string& domain : dnssl.domains) {
    if (domain.empty()) continue;
    size_t start = 0;
    while (start < domain.size()) {
      size_t dot = domain.find('.', start);
      if (dot == std::string::npos) dot = domain.size();
      size_t len = dot - start;
      if (len > 0 && len <= 63) {
        names.push_back(static_cast<char>(len));
        names.append(domain, start, len);
      }
      start = dot + 1;
    }
    names.push_back('\0');
  }
  size_t total_unpadded = 8 + names.size();
  size_t total_padded =
      ((total_unpadded + kOptionUnitBytes - 1) / kOptionUnitBytes) *
      kOptionUnitBytes;
  if (total_padded < 16) total_padded = 16;
  writer.WriteU8(static_cast<uint8>(NdpOptionType::DnsSearchList));
  writer.WriteU8(static_cast<uint8>(total_padded / kOptionUnitBytes));
  writer.WriteU16(0);
  writer.WriteU32(dnssl.lifetime_seconds);
  writer.WriteBytes(names);
  writer.WriteZeros(total_padded - total_unpadded);
}

}  // namespace

std::optional<NdpRouterSolicitation> ParseRouterSolicitation(
    const Ipv6Header& ipv6_header, std::string_view icmpv6_packet) {
  WireReader reader(icmpv6_packet);
  uint8 type = reader.ReadU8();
  uint8 code = reader.ReadU8();
  reader.Skip(6);
  if (!ValidateCommonNdpHeader(ipv6_header, Icmpv6Type::RouterSolicitation,
                               type, code, reader.ok()))
    return std::nullopt;

  NdpRouterSolicitation rs;
  bool ok = WalkNdpOptions(
      reader.Rest(), [&](NdpOptionType opt_type, std::string_view body) {
        if (opt_type == NdpOptionType::SourceLinkLayerAddress) {
          if (ipv6_header.source.IsUnspecified()) return false;
          auto mac = ParseHardwareAddressOption(body);
          if (!mac.has_value()) return false;
          rs.source_mac = *mac;
        }
        return true;
      });
  if (!ok) return std::nullopt;
  return rs;
}

std::string BuildRouterSolicitation(const IpAddress& source,
                                    const IpAddress& destination,
                                    const NdpRouterSolicitation& message) {
  std::string out;
  WireWriter writer(out);
  writer.WriteU8(static_cast<uint8>(Icmpv6Type::RouterSolicitation));
  writer.WriteU8(0);
  writer.WriteU16(0);
  writer.WriteU32(0);
  if (message.source_mac.has_value() && !source.IsUnspecified()) {
    AppendHardwareAddressOption(NdpOptionType::SourceLinkLayerAddress,
                                *message.source_mac, writer);
  }
  FinalizeIcmpv6Checksum(source, destination, out);
  return out;
}

std::optional<NdpRouterAdvertisement> ParseRouterAdvertisement(
    const Ipv6Header& ipv6_header, std::string_view icmpv6_packet) {
  if (!ipv6_header.source.IsLinkLocal()) return std::nullopt;
  WireReader reader(icmpv6_packet);
  uint8 type = reader.ReadU8();
  uint8 code = reader.ReadU8();
  reader.Skip(2);
  uint8 cur_hop_limit = reader.ReadU8();
  uint8 flags = reader.ReadU8();
  uint16 router_lifetime = reader.ReadU16();
  uint32 reachable_time = reader.ReadU32();
  uint32 retrans_timer = reader.ReadU32();
  if (!ValidateCommonNdpHeader(ipv6_header, Icmpv6Type::RouterAdvertisement,
                               type, code, reader.ok()))
    return std::nullopt;

  NdpRouterAdvertisement ra;
  ra.cur_hop_limit = cur_hop_limit;
  ra.managed_flag = (flags & kRaFlagManaged) != 0;
  ra.other_config_flag = (flags & kRaFlagOtherConfig) != 0;
  ra.router_lifetime_seconds = router_lifetime;
  ra.reachable_time_ms = reachable_time;
  ra.retrans_timer_ms = retrans_timer;

  bool ok = WalkNdpOptions(
      reader.Rest(), [&](NdpOptionType opt_type, std::string_view body) {
        switch (opt_type) {
          case NdpOptionType::SourceLinkLayerAddress: {
            auto mac = ParseHardwareAddressOption(body);
            if (!mac.has_value()) return false;
            ra.source_mac = *mac;
            return true;
          }
          case NdpOptionType::Mtu: {
            if (body.size() != kMtuBodySize) return false;
            WireReader opt_reader(body);
            opt_reader.Skip(2);
            uint32 mtu_val = opt_reader.ReadU32();
            if (mtu_val >= kIpv6MinimumMtu) ra.mtu = mtu_val;
            return true;
          }
          case NdpOptionType::PrefixInformation: {
            if (body.size() != kPrefixInfoBodySize) return false;
            WireReader opt_reader(body);
            NdpPrefixInformation pio;
            pio.prefix_length = opt_reader.ReadU8();
            if (pio.prefix_length > kMaxIpv6PrefixLength) return false;
            uint8 pio_flags = opt_reader.ReadU8();
            pio.on_link = (pio_flags & kPioFlagOnLink) != 0;
            pio.autonomous = (pio_flags & kPioFlagAutonomous) != 0;
            pio.valid_lifetime_seconds = opt_reader.ReadU32();
            pio.preferred_lifetime_seconds = opt_reader.ReadU32();
            opt_reader.Skip(4);
            pio.prefix = opt_reader.ReadIpv6Address();
            ra.prefixes.push_back(pio);
            return true;
          }
          case NdpOptionType::RecursiveDnsServer: {
            if (body.size() < kMinRdnssBodySize ||
                (body.size() - 6) % IpAddress::kV6Length != 0)
              return false;
            WireReader opt_reader(body);
            opt_reader.Skip(2);
            NdpRdnssOption rdnss;
            rdnss.lifetime_seconds = opt_reader.ReadU32();
            while (opt_reader.Remaining() >= IpAddress::kV6Length) {
              IpAddress srv = opt_reader.ReadIpv6Address();
              if (!srv.IsUnspecified() && !srv.IsMulticast())
                rdnss.servers.push_back(srv);
            }
            ra.rdnss.push_back(std::move(rdnss));
            return true;
          }
          case NdpOptionType::DnsSearchList: {
            if (body.size() < kMinDnsslBodySize) return false;
            WireReader opt_reader(body);
            opt_reader.Skip(2);
            NdpDnsslOption dnssl;
            dnssl.lifetime_seconds = opt_reader.ReadU32();
            if (!DecodeDnsslDomains(opt_reader.Rest(), dnssl.domains))
              return false;
            ra.dnssl.push_back(std::move(dnssl));
            return true;
          }
          case NdpOptionType::Nat64Prefix: {
            if (body.size() != kPref64BodySize) return false;
            WireReader opt_reader(body);
            uint16 sl_and_plc = opt_reader.ReadU16();
            std::string_view raw_prefix =
                opt_reader.ReadBytes(kPref64PrefixBytes);
            auto decoded_len = DecodePref64Plc(sl_and_plc & 0x07);
            if (!decoded_len.has_value()) return true;
            NdpPref64Option pref64;
            pref64.prefix_length = *decoded_len;
            pref64.lifetime_seconds =
                static_cast<uint32>(sl_and_plc >> 3) * kOptionUnitBytes;
            std::array<uint8, 16> addr_bytes{};
            for (size_t i = 0; i < kPref64PrefixBytes; i++)
              addr_bytes[i] = static_cast<uint8>(raw_prefix[i]);
            pref64.prefix = IpAddress::V6(addr_bytes);
            ra.pref64.push_back(pref64);
            return true;
          }
          default:
            return true;
        }
      });
  if (!ok) return std::nullopt;
  return ra;
}

std::string BuildRouterAdvertisement(const IpAddress& source,
                                     const IpAddress& destination,
                                     const NdpRouterAdvertisement& message) {
  std::string out;
  WireWriter writer(out);
  writer.WriteU8(static_cast<uint8>(Icmpv6Type::RouterAdvertisement));
  writer.WriteU8(0);
  writer.WriteU16(0);
  writer.WriteU8(message.cur_hop_limit);
  uint8 flags = (message.managed_flag ? kRaFlagManaged : 0) |
                (message.other_config_flag ? kRaFlagOtherConfig : 0);
  writer.WriteU8(flags);
  writer.WriteU16(message.router_lifetime_seconds);
  writer.WriteU32(message.reachable_time_ms);
  writer.WriteU32(message.retrans_timer_ms);

  if (message.source_mac.has_value()) {
    AppendHardwareAddressOption(NdpOptionType::SourceLinkLayerAddress,
                                *message.source_mac, writer);
  }
  if (message.mtu.has_value()) {
    writer.WriteU8(static_cast<uint8>(NdpOptionType::Mtu));
    writer.WriteU8(1);
    writer.WriteU16(0);
    writer.WriteU32(*message.mtu);
  }
  for (const NdpPrefixInformation& pio : message.prefixes) {
    writer.WriteU8(static_cast<uint8>(NdpOptionType::PrefixInformation));
    writer.WriteU8(4);
    writer.WriteU8(pio.prefix_length);
    uint8 pio_flags = (pio.on_link ? kPioFlagOnLink : 0) |
                      (pio.autonomous ? kPioFlagAutonomous : 0);
    writer.WriteU8(pio_flags);
    writer.WriteU32(pio.valid_lifetime_seconds);
    writer.WriteU32(pio.preferred_lifetime_seconds);
    writer.WriteU32(0);
    writer.WriteIpv6Address(pio.prefix);
  }
  for (const NdpRdnssOption& rdnss : message.rdnss) {
    if (rdnss.servers.empty()) continue;
    writer.WriteU8(static_cast<uint8>(NdpOptionType::RecursiveDnsServer));
    writer.WriteU8(static_cast<uint8>(1 + 2 * rdnss.servers.size()));
    writer.WriteU16(0);
    writer.WriteU32(rdnss.lifetime_seconds);
    for (const IpAddress& server : rdnss.servers)
      writer.WriteIpv6Address(server);
  }
  for (const NdpDnsslOption& dnssl : message.dnssl) {
    if (dnssl.domains.empty()) continue;
    AppendDnsslOption(dnssl, writer);
  }
  for (const NdpPref64Option& pref64 : message.pref64) {
    auto plc = EncodePref64Plc(pref64.prefix_length);
    if (!plc.has_value()) continue;
    uint32 clamped = std::min(pref64.lifetime_seconds, kMaxPref64LifetimeSeconds);
    uint16 scaled = static_cast<uint16>((clamped + 7) / 8);
    writer.WriteU8(static_cast<uint8>(NdpOptionType::Nat64Prefix));
    writer.WriteU8(2);
    writer.WriteU16(static_cast<uint16>((scaled << 3) | (*plc & 0x07)));
    const auto& bytes = pref64.prefix.bytes();
    for (size_t i = 0; i < kPref64PrefixBytes; i++) writer.WriteU8(bytes[i]);
  }

  FinalizeIcmpv6Checksum(source, destination, out);
  return out;
}

std::optional<NdpNeighborSolicitation> ParseNeighborSolicitation(
    const Ipv6Header& ipv6_header, std::string_view icmpv6_packet) {
  WireReader reader(icmpv6_packet);
  uint8 type = reader.ReadU8();
  uint8 code = reader.ReadU8();
  reader.Skip(6);
  IpAddress target = reader.ReadIpv6Address();
  if (!ValidateCommonNdpHeader(ipv6_header, Icmpv6Type::NeighborSolicitation,
                               type, code, reader.ok()))
    return std::nullopt;
  if (target.IsMulticast() || target.IsUnspecified()) return std::nullopt;
  if (ipv6_header.source.IsUnspecified() &&
      ipv6_header.destination != SolicitedNodeMulticastAddress(target))
    return std::nullopt;

  NdpNeighborSolicitation ns;
  ns.target = target;
  bool ok = WalkNdpOptions(
      reader.Rest(), [&](NdpOptionType opt_type, std::string_view body) {
        if (opt_type == NdpOptionType::SourceLinkLayerAddress) {
          if (ipv6_header.source.IsUnspecified()) return false;
          auto mac = ParseHardwareAddressOption(body);
          if (!mac.has_value()) return false;
          ns.source_mac = *mac;
        }
        return true;
      });
  if (!ok) return std::nullopt;
  return ns;
}

std::string BuildNeighborSolicitation(const IpAddress& source,
                                      const IpAddress& destination,
                                      const NdpNeighborSolicitation& message) {
  std::string out;
  WireWriter writer(out);
  writer.WriteU8(static_cast<uint8>(Icmpv6Type::NeighborSolicitation));
  writer.WriteU8(0);
  writer.WriteU16(0);
  writer.WriteU32(0);
  writer.WriteIpv6Address(message.target);
  if (message.source_mac.has_value() && !source.IsUnspecified()) {
    AppendHardwareAddressOption(NdpOptionType::SourceLinkLayerAddress,
                                *message.source_mac, writer);
  }
  FinalizeIcmpv6Checksum(source, destination, out);
  return out;
}

std::optional<NdpNeighborAdvertisement> ParseNeighborAdvertisement(
    const Ipv6Header& ipv6_header, std::string_view icmpv6_packet) {
  WireReader reader(icmpv6_packet);
  uint8 type = reader.ReadU8();
  uint8 code = reader.ReadU8();
  reader.Skip(2);
  uint32 flags = reader.ReadU32();
  IpAddress target = reader.ReadIpv6Address();
  if (!ValidateCommonNdpHeader(ipv6_header, Icmpv6Type::NeighborAdvertisement,
                               type, code, reader.ok()))
    return std::nullopt;
  if (target.IsMulticast() || target.IsUnspecified()) return std::nullopt;

  NdpNeighborAdvertisement na;
  na.router_flag = (flags & kNaFlagRouter) != 0;
  na.solicited_flag = (flags & kNaFlagSolicited) != 0;
  na.override_flag = (flags & kNaFlagOverride) != 0;
  na.target = target;
  if (ipv6_header.destination.IsMulticast() && na.solicited_flag)
    return std::nullopt;

  bool ok = WalkNdpOptions(
      reader.Rest(), [&](NdpOptionType opt_type, std::string_view body) {
        if (opt_type == NdpOptionType::TargetLinkLayerAddress) {
          auto mac = ParseHardwareAddressOption(body);
          if (!mac.has_value()) return false;
          na.target_mac = *mac;
        }
        return true;
      });
  if (!ok) return std::nullopt;
  return na;
}

std::string BuildNeighborAdvertisement(
    const IpAddress& source, const IpAddress& destination,
    const NdpNeighborAdvertisement& message) {
  std::string out;
  WireWriter writer(out);
  writer.WriteU8(static_cast<uint8>(Icmpv6Type::NeighborAdvertisement));
  writer.WriteU8(0);
  writer.WriteU16(0);
  uint32 flags = (message.router_flag ? kNaFlagRouter : 0) |
                 (message.solicited_flag ? kNaFlagSolicited : 0) |
                 (message.override_flag ? kNaFlagOverride : 0);
  writer.WriteU32(flags);
  writer.WriteIpv6Address(message.target);
  if (message.target_mac.has_value()) {
    AppendHardwareAddressOption(NdpOptionType::TargetLinkLayerAddress,
                                *message.target_mac, writer);
  }
  FinalizeIcmpv6Checksum(source, destination, out);
  return out;
}

std::optional<NdpRedirect> ParseRedirect(const Ipv6Header& ipv6_header,
                                         std::string_view icmpv6_packet) {
  if (!ipv6_header.source.IsLinkLocal()) return std::nullopt;
  WireReader reader(icmpv6_packet);
  uint8 type = reader.ReadU8();
  uint8 code = reader.ReadU8();
  reader.Skip(6);
  IpAddress target = reader.ReadIpv6Address();
  IpAddress destination = reader.ReadIpv6Address();
  if (!ValidateCommonNdpHeader(ipv6_header, Icmpv6Type::Redirect, type, code,
                               reader.ok()))
    return std::nullopt;
  if (destination.IsMulticast() || target.IsMulticast()) return std::nullopt;
  if (!target.IsLinkLocal() && target != destination) return std::nullopt;

  NdpRedirect redirect;
  redirect.target = target;
  redirect.destination = destination;
  bool ok = WalkNdpOptions(
      reader.Rest(), [&](NdpOptionType opt_type, std::string_view body) {
        if (opt_type == NdpOptionType::TargetLinkLayerAddress) {
          auto mac = ParseHardwareAddressOption(body);
          if (!mac.has_value()) return false;
          redirect.target_mac = *mac;
        }
        return true;
      });
  if (!ok) return std::nullopt;
  return redirect;
}

std::string BuildRedirect(const IpAddress& source, const IpAddress& destination,
                          const NdpRedirect& message) {
  std::string out;
  WireWriter writer(out);
  writer.WriteU8(static_cast<uint8>(Icmpv6Type::Redirect));
  writer.WriteU8(0);
  writer.WriteU16(0);
  writer.WriteU32(0);
  writer.WriteIpv6Address(message.target);
  writer.WriteIpv6Address(message.destination);
  if (message.target_mac.has_value()) {
    AppendHardwareAddressOption(NdpOptionType::TargetLinkLayerAddress,
                                *message.target_mac, writer);
  }
  FinalizeIcmpv6Checksum(source, destination, out);
  return out;
}

NudStateMachine::NudStateMachine(const IpAddress& local_source,
                                 const HardwareAddress& local_mac,
                                 Ipv6Sink send_sink)
    : local_source_(local_source),
      local_mac_(local_mac),
      send_sink_(std::move(send_sink)) {}

void NudStateMachine::SetLocalIdentity(const IpAddress& local_source,
                                       const HardwareAddress& local_mac) {
  local_source_ = local_source;
  local_mac_ = local_mac;
}

void NudStateMachine::SetTimers(std::chrono::milliseconds reachable_time,
                                std::chrono::milliseconds retrans_timer) {
  if (reachable_time.count() > 0) reachable_time_ = reachable_time;
  if (retrans_timer.count() > 0) retrans_timer_ = retrans_timer;
}

const NudEntry* NudStateMachine::Find(const IpAddress& neighbor) const {
  auto it = entries_.find(neighbor);
  if (it == entries_.end()) return nullptr;
  return &it->second;
}

std::optional<HardwareAddress> NudStateMachine::ResolveOrTouch(
    const IpAddress& neighbor, std::chrono::steady_clock::time_point now) {
  auto it = entries_.find(neighbor);
  if (it == entries_.end()) {
    NudEntry entry;
    entry.address = neighbor;
    entry.state = NudState::Incomplete;
    entry.probes_sent = 1;
    entry.deadline = now + retrans_timer_;
    entries_.emplace(neighbor, entry);
    SendSolicitation(neighbor, false, {});
    return std::nullopt;
  }

  NudEntry& entry = it->second;
  if (entry.state == NudState::Incomplete) return std::nullopt;
  if (entry.state == NudState::Reachable && now >= entry.deadline)
    entry.state = NudState::Stale;
  if (entry.state == NudState::Stale) {
    entry.state = NudState::Delay;
    entry.probes_sent = 0;
    entry.deadline = now + kDelayFirstProbeTime;
  }
  return entry.mac;
}

void NudStateMachine::RecordPassiveLinkLayerAddress(
    const IpAddress& neighbor, const HardwareAddress& mac, bool is_router,
    std::chrono::steady_clock::time_point now) {
  auto it = entries_.find(neighbor);
  if (it == entries_.end()) {
    NudEntry entry;
    entry.address = neighbor;
    entry.mac = mac;
    entry.state = NudState::Stale;
    entry.is_router = is_router;
    entries_.emplace(neighbor, entry);
    return;
  }

  NudEntry& entry = it->second;
  if (is_router) entry.is_router = true;
  if (entry.state == NudState::Incomplete || entry.mac != mac) {
    entry.mac = mac;
    entry.state = NudState::Stale;
    entry.probes_sent = 0;
    entry.deadline = std::chrono::steady_clock::time_point::max();
  }
  (void)now;
}

void NudStateMachine::OnNeighborAdvertisement(
    const NdpNeighborAdvertisement& advertisement,
    std::chrono::steady_clock::time_point now) {
  auto it = entries_.find(advertisement.target);
  if (it == entries_.end()) return;
  NudEntry& entry = it->second;

  if (entry.state == NudState::Incomplete) {
    if (!advertisement.target_mac.has_value()) return;
    entry.mac = *advertisement.target_mac;
    entry.is_router = advertisement.router_flag;
    entry.probes_sent = 0;
    if (advertisement.solicited_flag) {
      entry.state = NudState::Reachable;
      entry.deadline = now + reachable_time_;
    } else {
      entry.state = NudState::Stale;
      entry.deadline = std::chrono::steady_clock::time_point::max();
    }
    return;
  }

  bool same_mac = !advertisement.target_mac.has_value() ||
                  *advertisement.target_mac == entry.mac;
  if (advertisement.override_flag || same_mac) {
    if (advertisement.target_mac.has_value())
      entry.mac = *advertisement.target_mac;
    entry.is_router = advertisement.router_flag;
    if (advertisement.solicited_flag) {
      entry.state = NudState::Reachable;
      entry.probes_sent = 0;
      entry.deadline = now + reachable_time_;
    } else if (!same_mac) {
      entry.state = NudState::Stale;
      entry.probes_sent = 0;
      entry.deadline = std::chrono::steady_clock::time_point::max();
    }
  } else if (entry.state == NudState::Reachable) {
    entry.state = NudState::Stale;
    entry.probes_sent = 0;
    entry.deadline = std::chrono::steady_clock::time_point::max();
  }
}

void NudStateMachine::ConfirmReachability(
    const IpAddress& neighbor, std::chrono::steady_clock::time_point now) {
  auto it = entries_.find(neighbor);
  if (it == entries_.end() || it->second.state == NudState::Incomplete) return;
  it->second.state = NudState::Reachable;
  it->second.probes_sent = 0;
  it->second.deadline = now + reachable_time_;
}

void NudStateMachine::OnTimer(std::chrono::steady_clock::time_point now) {
  for (auto it = entries_.begin(); it != entries_.end();) {
    NudEntry& entry = it->second;
    if (now < entry.deadline) {
      ++it;
      continue;
    }

    switch (entry.state) {
      case NudState::Incomplete:
        if (entry.probes_sent >= kMaxMulticastSolicit) {
          it = entries_.erase(it);
          continue;
        }
        entry.probes_sent++;
        entry.deadline = now + retrans_timer_;
        SendSolicitation(entry.address, false, {});
        break;
      case NudState::Reachable:
        entry.state = NudState::Stale;
        entry.deadline = std::chrono::steady_clock::time_point::max();
        break;
      case NudState::Delay:
        entry.state = NudState::Probe;
        entry.probes_sent = 1;
        entry.deadline = now + retrans_timer_;
        SendSolicitation(entry.address, true, entry.mac);
        break;
      case NudState::Probe:
        if (entry.probes_sent >= kMaxUnicastSolicit) {
          it = entries_.erase(it);
          continue;
        }
        entry.probes_sent++;
        entry.deadline = now + retrans_timer_;
        SendSolicitation(entry.address, true, entry.mac);
        break;
      case NudState::Stale:
        entry.deadline = std::chrono::steady_clock::time_point::max();
        break;
    }
    ++it;
  }
}

std::optional<std::chrono::steady_clock::time_point>
NudStateMachine::NextDeadline() const {
  std::optional<std::chrono::steady_clock::time_point> earliest;
  for (const auto& [addr, entry] : entries_) {
    if (entry.deadline == std::chrono::steady_clock::time_point::max())
      continue;
    if (!earliest.has_value() || entry.deadline < *earliest)
      earliest = entry.deadline;
  }
  return earliest;
}

void NudStateMachine::SendSolicitation(const IpAddress& target, bool unicast,
                                       const HardwareAddress& unused_mac) {
  (void)unused_mac;
  if (!send_sink_) return;
  IpAddress dst = unicast ? target : SolicitedNodeMulticastAddress(target);
  NdpNeighborSolicitation ns;
  ns.target = target;
  ns.source_mac = local_mac_;

  Ipv6Datagram datagram;
  datagram.source = local_source_;
  datagram.destination = dst;
  datagram.next_header = static_cast<uint8>(Ipv6NextHeader::Icmpv6);
  datagram.hop_limit = kNdpHopLimit;
  datagram.payload = BuildNeighborSolicitation(local_source_, dst, ns);
  send_sink_(std::move(datagram));
}

RouterAdvertiser::RouterAdvertiser(Ipv6Sink send_sink)
    : send_sink_(std::move(send_sink)) {}

void RouterAdvertiser::SetConfig(const RouterAdvertisementConfig& config,
                                 std::chrono::steady_clock::time_point now) {
  config_ = config;
  if (!config_.enabled || !config_.link_local_source.IsLinkLocal()) {
    initial_advertisements_remaining_ = 0;
    next_ra_deadline_.reset();
    return;
  }
  initial_advertisements_remaining_ = kMaxInitialRtrAdvertisements - 1;
  SendAdvertisement(AllNodesMulticastAddress(), now);
  last_multicast_ra_ = now;
  next_ra_deadline_ = now + std::min(kMaxInitialRtrAdvertInterval,
                                     config_.adv_interval);
}

void RouterAdvertiser::OnRouterSolicitation(
    const Ipv6Header& ipv6_header, const NdpRouterSolicitation& solicitation,
    std::chrono::steady_clock::time_point now) {
  (void)solicitation;
  if (!config_.enabled || !config_.link_local_source.IsLinkLocal()) return;

  // Reply directly to the soliciting node when it has a unicast address, or to
  // all-nodes multicast (rate-limited by kMinDelayBetweenRas) when unspecified.
  if (!ipv6_header.source.IsUnspecified() && !ipv6_header.source.IsMulticast()) {
    SendAdvertisement(ipv6_header.source, now);
    return;
  }

  if (last_multicast_ra_.has_value() &&
      now - *last_multicast_ra_ < kMinDelayBetweenRas) {
    auto earliest_allowed = *last_multicast_ra_ + kMinDelayBetweenRas;
    if (!next_ra_deadline_.has_value() || earliest_allowed < *next_ra_deadline_)
      next_ra_deadline_ = earliest_allowed;
    return;
  }
  SendAdvertisement(AllNodesMulticastAddress(), now);
  last_multicast_ra_ = now;
}

void RouterAdvertiser::OnTimer(std::chrono::steady_clock::time_point now) {
  if (!config_.enabled || !next_ra_deadline_.has_value() ||
      now < *next_ra_deadline_)
    return;

  SendAdvertisement(AllNodesMulticastAddress(), now);
  last_multicast_ra_ = now;
  if (initial_advertisements_remaining_ > 0) {
    initial_advertisements_remaining_--;
    next_ra_deadline_ =
        now + std::min(kMaxInitialRtrAdvertInterval, config_.adv_interval);
  } else {
    next_ra_deadline_ = now + config_.adv_interval;
  }
}

std::optional<std::chrono::steady_clock::time_point>
RouterAdvertiser::NextDeadline() const {
  return next_ra_deadline_;
}

void RouterAdvertiser::SendAdvertisement(
    const IpAddress& destination, std::chrono::steady_clock::time_point now) {
  (void)now;
  if (!send_sink_) return;
  NdpRouterAdvertisement ra;
  ra.cur_hop_limit = config_.cur_hop_limit;
  ra.managed_flag = config_.managed_flag;
  ra.other_config_flag = config_.other_config_flag;
  ra.router_lifetime_seconds = config_.router_lifetime_seconds;
  ra.reachable_time_ms = config_.reachable_time_ms;
  ra.retrans_timer_ms = config_.retrans_timer_ms;
  ra.source_mac = config_.source_mac;
  ra.mtu = config_.mtu;
  ra.prefixes = config_.prefixes;
  ra.rdnss = config_.rdnss;
  ra.dnssl = config_.dnssl;
  ra.pref64 = config_.pref64;

  Ipv6Datagram datagram;
  datagram.source = config_.link_local_source;
  datagram.destination = destination;
  datagram.next_header = static_cast<uint8>(Ipv6NextHeader::Icmpv6);
  datagram.hop_limit = kNdpHopLimit;
  datagram.payload =
      BuildRouterAdvertisement(config_.link_local_source, destination, ra);
  send_sink_(std::move(datagram));
}
