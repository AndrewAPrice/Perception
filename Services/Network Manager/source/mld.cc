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

#include "mld.h"

#include <algorithm>

#include "icmpv6.h"
#include "wire_format.h"

using ::perception::network::IpAddress;

namespace {

// Exact size of an MLDv1 message (Query, Report, or Done).
constexpr size_t kMldv1MessageSize = 24;

// Minimum size of an MLDv2 Query message.
constexpr size_t kMldv2QueryMinSize = 28;

// Header size of an MLDv2 Report message before address records.
constexpr size_t kMldv2ReportHeaderSize = 8;

// Threshold for floating-point encoding of MLDv2 Max Response Code.
constexpr uint16 kMaxRespFloatingPointBit = 0x8000u;

// Threshold for floating-point encoding of MLDv2 QQIC.
constexpr uint8 kQqicFloatingPointBit = 0x80u;

// Minimum multicast scope that MLD reports (2 = link-local).
constexpr uint8 kLinkLocalScope = 2;

// Default Querier's Robustness Variable (RFC 3810 §9.1).
constexpr uint8 kDefaultRobustnessVariable = 2;

// Default Querier's Query Interval in seconds (RFC 3810 §9.2).
constexpr uint32 kDefaultQueryIntervalSeconds = 125;

uint32 DecodeMldv2Qqic(uint8 qqic) {
  if (qqic == 0) return kDefaultQueryIntervalSeconds;
  if ((qqic & kQqicFloatingPointBit) == 0) return qqic;
  uint32 exp = (qqic >> 4) & 0x07;
  uint32 mant = qqic & 0x0F;
  return (mant | 0x10u) << (exp + 3);
}

}  // namespace

std::chrono::milliseconds DecodeMldv2MaxResponseCode(uint16 code) {
  if ((code & kMaxRespFloatingPointBit) == 0)
    return std::chrono::milliseconds(code);
  uint32 exp = (code >> 12) & 0x07;
  uint32 mant = code & 0x0FFF;
  uint32 ms = (mant | 0x1000u) << (exp + 3);
  return std::chrono::milliseconds(ms);
}

std::optional<MldQuery> ParseMldQuery(const Ipv6Header& ipv6_header,
                                      bool has_router_alert,
                                      std::string_view icmpv6_packet) {
  if (ipv6_header.hop_limit != kMldHopLimit || !has_router_alert)
    return std::nullopt;
  if (!ipv6_header.source.IsLinkLocal()) return std::nullopt;
  if (icmpv6_packet.size() < kMldv1MessageSize) return std::nullopt;

  WireReader reader(icmpv6_packet);
  uint8 type = reader.ReadU8();
  uint8 code = reader.ReadU8();
  reader.Skip(2);
  uint16 max_resp_code = reader.ReadU16();
  reader.Skip(2);
  IpAddress mcast_addr = reader.ReadIpv6Address();
  if (!reader.ok() ||
      type != static_cast<uint8>(Icmpv6Type::MulticastListenerQuery) ||
      code != 0)
    return std::nullopt;
  if (!mcast_addr.IsUnspecified() && !mcast_addr.IsMulticast())
    return std::nullopt;

  MldQuery query;
  query.multicast_address = mcast_addr;
  if (icmpv6_packet.size() == kMldv1MessageSize) {
    query.is_v1 = true;
    query.max_response_delay = std::chrono::milliseconds(max_resp_code);
    return query;
  }

  if (icmpv6_packet.size() < kMldv2QueryMinSize) return std::nullopt;
  query.is_v1 = false;
  query.max_response_delay = DecodeMldv2MaxResponseCode(max_resp_code);
  uint8 resv_s_qrv = reader.ReadU8();
  query.suppress_router_processing = (resv_s_qrv & 0x08) != 0;
  uint8 qrv = resv_s_qrv & 0x07;
  query.qrv = (qrv == 0) ? kDefaultRobustnessVariable : qrv;
  query.qqi_seconds = DecodeMldv2Qqic(reader.ReadU8());
  uint16 num_sources = reader.ReadU16();
  if (reader.Remaining() !=
      static_cast<size_t>(num_sources) * IpAddress::kV6Length)
    return std::nullopt;
  for (uint16 i = 0; i < num_sources; i++)
    query.sources.push_back(reader.ReadIpv6Address());
  if (!reader.ok()) return std::nullopt;
  return query;
}

std::string BuildMldv2Report(const IpAddress& source,
                             const IpAddress& destination,
                             const std::vector<MldAddressRecord>& records) {
  std::string out;
  WireWriter writer(out);
  writer.WriteU8(static_cast<uint8>(Icmpv6Type::MulticastListenerReportV2));
  writer.WriteU8(0);
  writer.WriteU16(0);
  writer.WriteU16(0);
  writer.WriteU16(static_cast<uint16>(records.size()));
  for (const MldAddressRecord& record : records) {
    writer.WriteU8(static_cast<uint8>(record.record_type));
    writer.WriteU8(0);
    writer.WriteU16(static_cast<uint16>(record.sources.size()));
    writer.WriteIpv6Address(record.multicast_address);
    for (const IpAddress& src : record.sources)
      writer.WriteIpv6Address(src);
  }
  FinalizeIcmpv6Checksum(source, destination, out);
  return out;
}

std::optional<std::vector<MldAddressRecord>> ParseMldv2Report(
    std::string_view icmpv6_packet) {
  if (icmpv6_packet.size() < kMldv2ReportHeaderSize) return std::nullopt;
  WireReader reader(icmpv6_packet);
  uint8 type = reader.ReadU8();
  uint8 code = reader.ReadU8();
  reader.Skip(4);
  uint16 count = reader.ReadU16();
  if (!reader.ok() ||
      type != static_cast<uint8>(Icmpv6Type::MulticastListenerReportV2) ||
      code != 0)
    return std::nullopt;

  std::vector<MldAddressRecord> records;
  records.reserve(count);
  for (uint16 i = 0; i < count; i++) {
    MldAddressRecord rec;
    rec.record_type = static_cast<MldRecordType>(reader.ReadU8());
    uint8 aux_data_words = reader.ReadU8();
    uint16 num_sources = reader.ReadU16();
    rec.multicast_address = reader.ReadIpv6Address();
    if (!reader.ok()) return std::nullopt;
    for (uint16 s = 0; s < num_sources; s++)
      rec.sources.push_back(reader.ReadIpv6Address());
    reader.Skip(static_cast<size_t>(aux_data_words) * 4);
    if (!reader.ok()) return std::nullopt;
    records.push_back(std::move(rec));
  }
  return records;
}

std::string BuildMldv1Message(const IpAddress& source,
                              const IpAddress& destination, bool is_done,
                              const IpAddress& multicast_address) {
  std::string out;
  out.reserve(kMldv1MessageSize);
  WireWriter writer(out);
  writer.WriteU8(static_cast<uint8>(
      is_done ? Icmpv6Type::MulticastListenerDone
              : Icmpv6Type::MulticastListenerReportV1));
  writer.WriteU8(0);
  writer.WriteU16(0);
  writer.WriteU16(0);
  writer.WriteU16(0);
  writer.WriteIpv6Address(multicast_address);
  FinalizeIcmpv6Checksum(source, destination, out);
  return out;
}

bool IsMldReportableGroup(const IpAddress& group) {
  if (!group.IsV6() || !group.IsMulticast()) return false;
  if (MulticastScope(group) < kLinkLocalScope) return false;
  if (group == AllNodesMulticastAddress()) return false;
  return true;
}

MldManager::MldManager(Ipv6Sink send_sink, MldRandomDelayFn random_delay)
    : send_sink_(std::move(send_sink)),
      random_delay_(std::move(random_delay)) {}

void MldManager::SetLinkLocalSource(const IpAddress& source) {
  link_local_source_ = source;
}

bool MldManager::JoinGroup(const IpAddress& group,
                           std::chrono::steady_clock::time_point now) {
  if (!group.IsV6() || !group.IsMulticast()) return false;
  int& count = joined_groups_[group];
  count++;
  if (count > 1) return false;

  if (!IsMldReportableGroup(group)) return true;

  if (IsMldv1Mode(now)) {
    SendV1Message(group, false);
  } else {
    SendV2Report({MldAddressRecord{MldRecordType::ChangeToExcludeMode, group, {}}});
  }

  PendingGroupAction action;
  action.record_type = MldRecordType::ChangeToExcludeMode;
  action.remaining_transmissions = 1;
  action.deadline = now + PickDelay(kUnsolicitedReportInterval);
  pending_[group] = action;
  return true;
}

bool MldManager::LeaveGroup(const IpAddress& group,
                            std::chrono::steady_clock::time_point now) {
  auto it = joined_groups_.find(group);
  if (it == joined_groups_.end()) return false;
  it->second--;
  if (it->second > 0) return false;
  joined_groups_.erase(it);

  if (!IsMldReportableGroup(group)) {
    pending_.erase(group);
    return true;
  }

  if (IsMldv1Mode(now)) {
    pending_.erase(group);
    if (!link_local_source_.IsUnspecified()) SendV1Message(group, true);
  } else {
    SendV2Report({MldAddressRecord{MldRecordType::ChangeToIncludeMode, group, {}}});
    PendingGroupAction action;
    action.record_type = MldRecordType::ChangeToIncludeMode;
    action.remaining_transmissions = 1;
    action.deadline = now + PickDelay(kUnsolicitedReportInterval);
    pending_[group] = action;
  }
  return true;
}

bool MldManager::IsJoined(const IpAddress& group) const {
  return joined_groups_.find(group) != joined_groups_.end();
}

void MldManager::OnQuery(const MldQuery& query,
                         std::chrono::steady_clock::time_point now) {
  if (query.is_v1) mldv1_compat_until_ = now + kMldv1CompatibilityTimeout;

  auto delay = PickDelay(query.max_response_delay);
  auto target_time = now + delay;

  if (query.multicast_address.IsUnspecified()) {
    if (!general_query_deadline_.has_value() ||
        target_time < *general_query_deadline_)
      general_query_deadline_ = target_time;
    return;
  }

  if (!IsJoined(query.multicast_address) ||
      !IsMldReportableGroup(query.multicast_address))
    return;

  auto it = pending_.find(query.multicast_address);
  if (it != pending_.end() && it->second.deadline <= target_time) return;

  PendingGroupAction action;
  action.record_type = MldRecordType::ModeIsExclude;
  action.remaining_transmissions = 1;
  action.deadline = target_time;
  pending_[query.multicast_address] = action;
}

void MldManager::OnTimer(std::chrono::steady_clock::time_point now) {
  if (mldv1_compat_until_.has_value() && now >= *mldv1_compat_until_)
    mldv1_compat_until_.reset();

  bool v1_mode = IsMldv1Mode(now);
  if (general_query_deadline_.has_value() && now >= *general_query_deadline_) {
    general_query_deadline_.reset();
    std::vector<MldAddressRecord> records;
    for (const auto& [group, count] : joined_groups_) {
      if (!IsMldReportableGroup(group)) continue;
      if (v1_mode)
        SendV1Message(group, false);
      else
        records.push_back(
            MldAddressRecord{MldRecordType::ModeIsExclude, group, {}});
    }
    if (!records.empty()) SendV2Report(records);
  }

  std::vector<MldAddressRecord> v2_due;
  for (auto it = pending_.begin(); it != pending_.end();) {
    PendingGroupAction& action = it->second;
    if (now < action.deadline) {
      ++it;
      continue;
    }
    if (v1_mode) {
      if (IsJoined(it->first)) SendV1Message(it->first, false);
    } else {
      v2_due.push_back(MldAddressRecord{action.record_type, it->first, {}});
    }
    if (action.remaining_transmissions > 1) {
      action.remaining_transmissions--;
      action.deadline = now + PickDelay(kUnsolicitedReportInterval);
      ++it;
    } else {
      it = pending_.erase(it);
    }
  }
  if (!v2_due.empty()) SendV2Report(v2_due);
}

bool MldManager::IsMldv1Mode(std::chrono::steady_clock::time_point now) const {
  return mldv1_compat_until_.has_value() && now < *mldv1_compat_until_;
}

std::optional<std::chrono::steady_clock::time_point>
MldManager::NextDeadline() const {
  std::optional<std::chrono::steady_clock::time_point> earliest =
      general_query_deadline_;
  for (const auto& [group, action] : pending_) {
    if (!earliest.has_value() || action.deadline < *earliest)
      earliest = action.deadline;
  }
  return earliest;
}

void MldManager::SendV2Report(const std::vector<MldAddressRecord>& records) {
  if (!send_sink_ || records.empty()) return;
  IpAddress dst = AllMldv2RoutersMulticastAddress();
  Ipv6Datagram datagram;
  datagram.source = link_local_source_;
  datagram.destination = dst;
  datagram.next_header = static_cast<uint8>(Ipv6NextHeader::Icmpv6);
  datagram.hop_limit = kMldHopLimit;
  datagram.router_alert = true;
  datagram.payload = BuildMldv2Report(link_local_source_, dst, records);
  send_sink_(std::move(datagram));
}

void MldManager::SendV1Message(const IpAddress& group, bool is_done) {
  if (!send_sink_) return;
  IpAddress dst = is_done ? AllRoutersMulticastAddress() : group;
  Ipv6Datagram datagram;
  datagram.source = link_local_source_;
  datagram.destination = dst;
  datagram.next_header = static_cast<uint8>(Ipv6NextHeader::Icmpv6);
  datagram.hop_limit = kMldHopLimit;
  datagram.router_alert = true;
  datagram.payload = BuildMldv1Message(link_local_source_, dst, is_done, group);
  send_sink_(std::move(datagram));
}

std::chrono::milliseconds MldManager::PickDelay(
    std::chrono::milliseconds max_delay) {
  if (max_delay.count() <= 0) return std::chrono::milliseconds(0);
  uint32 max_ms = static_cast<uint32>(max_delay.count());
  if (random_delay_) {
    uint32 chosen = std::min(random_delay_(max_ms), max_ms);
    return std::chrono::milliseconds(chosen);
  }
  return std::chrono::milliseconds(max_ms / 2);
}
