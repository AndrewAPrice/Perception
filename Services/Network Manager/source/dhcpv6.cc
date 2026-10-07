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

#include "dhcpv6.h"

#include <algorithm>

#include "wire_format.h"

using ::perception::network::IpAddress;

namespace {

// DUID type 3: Link-layer address (DUID-LL, RFC 8415 §11.4).
constexpr uint16 kDuidTypeLinkLayer = 3;

// IANA hardware type 1: Ethernet (10Mb).
constexpr uint16 kHardwareTypeEthernet = 1;

// Minimum DHCPv6 message size (1-byte msg-type + 3-byte transaction-id).
constexpr size_t kMinDhcpv6MessageSize = 4;

// Fixed header size inside an IA_NA option before sub-options (IAID, T1, T2).
constexpr size_t kIaNaFixedBodySize = 12;

// Fixed size of an IA Address option body before sub-options (16B IP + 8B lifetimes).
constexpr size_t kIaAddressFixedBodySize = 24;

// Prefix length assigned to individual DHCPv6 IA_NA addresses (/128).
constexpr uint8 kDhcpv6AddressPrefixLength = 128;

// Default T1 fraction of shortest preferred lifetime (0.5 = 1/2).
constexpr uint32 kDefaultT1Numerator = 5;

// Default T2 fraction of shortest preferred lifetime (0.8 = 4/5).
constexpr uint32 kDefaultT2Numerator = 8;

// Denominator for default T1/T2 fractions.
constexpr uint32 kDefaultTimeFractionDenominator = 10;

// Maximum value of the 16-bit Elapsed Time option (0xFFFF centiseconds).
constexpr uint32 kMaxElapsedCentiseconds = 0xFFFFu;

bool DecodeDomainSearchList(std::string_view encoded,
                            std::vector<std::string>& out_domains) {
  size_t pos = 0;
  while (pos < encoded.size()) {
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

std::string EncodeDomainSearchList(const std::vector<std::string>& domains) {
  std::string out;
  for (const std::string& domain : domains) {
    if (domain.empty()) continue;
    size_t start = 0;
    while (start < domain.size()) {
      size_t dot = domain.find('.', start);
      if (dot == std::string::npos) dot = domain.size();
      size_t len = dot - start;
      if (len > 0 && len <= 63) {
        out.push_back(static_cast<char>(len));
        out.append(domain, start, len);
      }
      start = dot + 1;
    }
    out.push_back('\0');
  }
  return out;
}

bool ParseIaNaOption(std::string_view body, Dhcpv6IaNa& out_ia) {
  if (body.size() < kIaNaFixedBodySize) return false;
  WireReader reader(body);
  out_ia.iaid = reader.ReadU32();
  out_ia.t1_seconds = reader.ReadU32();
  out_ia.t2_seconds = reader.ReadU32();

  while (reader.Remaining() > 0) {
    uint16 sub_code = reader.ReadU16();
    uint16 sub_len = reader.ReadU16();
    std::string_view sub_body = reader.ReadBytes(sub_len);
    if (!reader.ok()) return false;

    if (sub_code == static_cast<uint16>(Dhcpv6OptionCode::IaAddress)) {
      if (sub_body.size() < kIaAddressFixedBodySize) return false;
      WireReader addr_reader(sub_body);
      Dhcpv6IaAddress ia_addr;
      ia_addr.address = addr_reader.ReadIpv6Address();
      ia_addr.preferred_lifetime_seconds = addr_reader.ReadU32();
      ia_addr.valid_lifetime_seconds = addr_reader.ReadU32();
      if (!addr_reader.ok()) return false;
      out_ia.addresses.push_back(ia_addr);
    } else if (sub_code == static_cast<uint16>(Dhcpv6OptionCode::StatusCode)) {
      if (sub_body.size() < 2) return false;
      WireReader st_reader(sub_body);
      out_ia.status_code = st_reader.ReadU16();
    }
  }
  return true;
}

void AppendIaNaOption(const Dhcpv6IaNa& ia, WireWriter& writer) {
  std::string body;
  WireWriter body_writer(body);
  body_writer.WriteU32(ia.iaid);
  body_writer.WriteU32(ia.t1_seconds);
  body_writer.WriteU32(ia.t2_seconds);
  for (const Dhcpv6IaAddress& addr : ia.addresses) {
    body_writer.WriteU16(static_cast<uint16>(Dhcpv6OptionCode::IaAddress));
    body_writer.WriteU16(static_cast<uint16>(kIaAddressFixedBodySize));
    body_writer.WriteIpv6Address(addr.address);
    body_writer.WriteU32(addr.preferred_lifetime_seconds);
    body_writer.WriteU32(addr.valid_lifetime_seconds);
  }
  if (ia.status_code != 0) {
    body_writer.WriteU16(static_cast<uint16>(Dhcpv6OptionCode::StatusCode));
    body_writer.WriteU16(2);
    body_writer.WriteU16(ia.status_code);
  }
  writer.WriteU16(static_cast<uint16>(Dhcpv6OptionCode::IaNa));
  writer.WriteU16(static_cast<uint16>(body.size()));
  writer.WriteBytes(body);
}

}  // namespace

std::string BuildDuidLinkLayer(const HardwareAddress& mac) {
  std::string out;
  out.reserve(4 + mac.size());
  WireWriter writer(out);
  writer.WriteU16(kDuidTypeLinkLayer);
  writer.WriteU16(kHardwareTypeEthernet);
  for (uint8 byte : mac) writer.WriteU8(byte);
  return out;
}

std::optional<Dhcpv6Message> ParseDhcpv6Message(std::string_view udp_payload) {
  if (udp_payload.size() < kMinDhcpv6MessageSize) return std::nullopt;
  WireReader reader(udp_payload);
  uint8 msg_type = reader.ReadU8();
  uint32 tx_id = reader.ReadU24();
  if (!reader.ok() || msg_type < 1 || msg_type > 11) return std::nullopt;

  Dhcpv6Message msg;
  msg.type = static_cast<Dhcpv6MessageType>(msg_type);
  msg.transaction_id = tx_id;

  while (reader.Remaining() > 0) {
    uint16 opt_code = reader.ReadU16();
    uint16 opt_len = reader.ReadU16();
    std::string_view body = reader.ReadBytes(opt_len);
    if (!reader.ok()) return std::nullopt;

    switch (static_cast<Dhcpv6OptionCode>(opt_code)) {
      case Dhcpv6OptionCode::ClientId:
        if (body.empty()) return std::nullopt;
        msg.client_id = std::string(body);
        break;
      case Dhcpv6OptionCode::ServerId:
        if (body.empty()) return std::nullopt;
        msg.server_id = std::string(body);
        break;
      case Dhcpv6OptionCode::IaNa: {
        Dhcpv6IaNa ia;
        if (!ParseIaNaOption(body, ia)) return std::nullopt;
        msg.ia_nas.push_back(std::move(ia));
        break;
      }
      case Dhcpv6OptionCode::OptionRequest: {
        if (body.size() % 2 != 0) return std::nullopt;
        WireReader oro_reader(body);
        while (oro_reader.Remaining() >= 2)
          msg.requested_options.push_back(oro_reader.ReadU16());
        break;
      }
      case Dhcpv6OptionCode::Preference:
        if (body.size() != 1) return std::nullopt;
        msg.preference = static_cast<uint8>(body[0]);
        break;
      case Dhcpv6OptionCode::ElapsedTime: {
        if (body.size() != 2) return std::nullopt;
        WireReader el_reader(body);
        msg.elapsed_time_centiseconds = el_reader.ReadU16();
        break;
      }
      case Dhcpv6OptionCode::StatusCode: {
        if (body.size() < 2) return std::nullopt;
        WireReader st_reader(body);
        msg.status_code = st_reader.ReadU16();
        break;
      }
      case Dhcpv6OptionCode::DnsServers: {
        if (body.size() % IpAddress::kV6Length != 0) return std::nullopt;
        WireReader dns_reader(body);
        while (dns_reader.Remaining() >= IpAddress::kV6Length) {
          IpAddress srv = dns_reader.ReadIpv6Address();
          if (!srv.IsUnspecified() && !srv.IsMulticast())
            msg.dns_servers.push_back(srv);
        }
        break;
      }
      case Dhcpv6OptionCode::DomainList:
        if (!DecodeDomainSearchList(body, msg.domain_search_list))
          return std::nullopt;
        break;
      case Dhcpv6OptionCode::InformationRefreshTime: {
        if (body.size() != 4) return std::nullopt;
        WireReader irt_reader(body);
        msg.info_refresh_time_seconds = irt_reader.ReadU32();
        break;
      }
      default:
        break;
    }
  }
  return msg;
}

std::string BuildDhcpv6Message(const Dhcpv6Message& message) {
  std::string out;
  WireWriter writer(out);
  writer.WriteU8(static_cast<uint8>(message.type));
  writer.WriteU24(message.transaction_id & 0xFFFFFFu);

  if (!message.client_id.empty()) {
    writer.WriteU16(static_cast<uint16>(Dhcpv6OptionCode::ClientId));
    writer.WriteU16(static_cast<uint16>(message.client_id.size()));
    writer.WriteBytes(message.client_id);
  }
  if (!message.server_id.empty()) {
    writer.WriteU16(static_cast<uint16>(Dhcpv6OptionCode::ServerId));
    writer.WriteU16(static_cast<uint16>(message.server_id.size()));
    writer.WriteBytes(message.server_id);
  }
  if (message.elapsed_time_centiseconds.has_value()) {
    writer.WriteU16(static_cast<uint16>(Dhcpv6OptionCode::ElapsedTime));
    writer.WriteU16(2);
    writer.WriteU16(*message.elapsed_time_centiseconds);
  }
  if (!message.requested_options.empty()) {
    writer.WriteU16(static_cast<uint16>(Dhcpv6OptionCode::OptionRequest));
    writer.WriteU16(static_cast<uint16>(message.requested_options.size() * 2));
    for (uint16 code : message.requested_options) writer.WriteU16(code);
  }
  if (message.preference.has_value()) {
    writer.WriteU16(static_cast<uint16>(Dhcpv6OptionCode::Preference));
    writer.WriteU16(1);
    writer.WriteU8(*message.preference);
  }
  if (message.status_code.has_value()) {
    writer.WriteU16(static_cast<uint16>(Dhcpv6OptionCode::StatusCode));
    writer.WriteU16(2);
    writer.WriteU16(*message.status_code);
  }
  for (const Dhcpv6IaNa& ia : message.ia_nas)
    AppendIaNaOption(ia, writer);
  if (!message.dns_servers.empty()) {
    writer.WriteU16(static_cast<uint16>(Dhcpv6OptionCode::DnsServers));
    writer.WriteU16(
        static_cast<uint16>(message.dns_servers.size() * IpAddress::kV6Length));
    for (const IpAddress& srv : message.dns_servers)
      writer.WriteIpv6Address(srv);
  }
  if (!message.domain_search_list.empty()) {
    std::string encoded = EncodeDomainSearchList(message.domain_search_list);
    writer.WriteU16(static_cast<uint16>(Dhcpv6OptionCode::DomainList));
    writer.WriteU16(static_cast<uint16>(encoded.size()));
    writer.WriteBytes(encoded);
  }
  if (message.info_refresh_time_seconds.has_value()) {
    writer.WriteU16(
        static_cast<uint16>(Dhcpv6OptionCode::InformationRefreshTime));
    writer.WriteU16(4);
    writer.WriteU32(*message.info_refresh_time_seconds);
  }
  return out;
}

Dhcpv6Client::Dhcpv6Client(const HardwareAddress& mac, Dhcpv6SendFn send_fn,
                           Dhcpv6TransactionIdFn tx_id_fn)
    : client_duid_(BuildDuidLinkLayer(mac)),
      iaid_((static_cast<uint32>(mac[2]) << 24) |
            (static_cast<uint32>(mac[3]) << 16) |
            (static_cast<uint32>(mac[4]) << 8) | static_cast<uint32>(mac[5])),
      send_fn_(std::move(send_fn)),
      tx_id_fn_(std::move(tx_id_fn)) {}

void Dhcpv6Client::OnRouterAdvertisementFlags(
    bool managed_flag, bool other_config_flag,
    std::chrono::steady_clock::time_point now) {
  if (managed_flag) {
    if (state_ == Dhcpv6State::Idle ||
        state_ == Dhcpv6State::StatelessRequesting ||
        state_ == Dhcpv6State::StatelessBound)
      StartStateful(now);
    return;
  }
  if ((other_config_flag || kForceDhcpv6InformationRequest) &&
      state_ == Dhcpv6State::Idle)
    StartStateless(now);
}

void Dhcpv6Client::StartStateless(std::chrono::steady_clock::time_point now) {
  state_ = Dhcpv6State::StatelessRequesting;
  active_tx_id_ = NextTransactionId();
  exchange_start_ = now;
  current_rto_ = kInitialRetransmitTimeout;
  retries_ = 0;
  refresh_deadline_.reset();
  SendCurrentExchangeMessage(now);
}

void Dhcpv6Client::StartStateful(std::chrono::steady_clock::time_point now) {
  state_ = Dhcpv6State::Soliciting;
  active_tx_id_ = NextTransactionId();
  exchange_start_ = now;
  current_rto_ = kInitialRetransmitTimeout;
  retries_ = 0;
  server_duid_.clear();
  t1_deadline_.reset();
  t2_deadline_.reset();
  refresh_deadline_.reset();
  SendCurrentExchangeMessage(now);
}

void Dhcpv6Client::Release(std::chrono::steady_clock::time_point now) {
  if (!addresses_.empty() && !server_duid_.empty() && send_fn_) {
    Dhcpv6Message rel;
    rel.type = Dhcpv6MessageType::Release;
    rel.transaction_id = NextTransactionId();
    rel.client_id = client_duid_;
    rel.server_id = server_duid_;
    rel.elapsed_time_centiseconds = 0;
    Dhcpv6IaNa ia;
    ia.iaid = iaid_;
    for (const InterfaceAddress& addr : addresses_) {
      Dhcpv6IaAddress ia_addr;
      ia_addr.address = addr.address;
      ia.addresses.push_back(ia_addr);
    }
    rel.ia_nas.push_back(std::move(ia));
    send_fn_(BuildDhcpv6Message(rel));
  }
  addresses_.clear();
  server_duid_.clear();
  retransmit_deadline_.reset();
  t1_deadline_.reset();
  t2_deadline_.reset();
  refresh_deadline_.reset();
  state_ = Dhcpv6State::Idle;
  (void)now;
}

void Dhcpv6Client::OnPacket(std::string_view udp_payload,
                            std::chrono::steady_clock::time_point now) {
  auto parsed = ParseDhcpv6Message(udp_payload);
  if (!parsed.has_value()) return;
  const Dhcpv6Message& msg = *parsed;

  // Reject messages that do not match the client's DUID or active transaction
  // ID (RFC 8415 §16). Reconfigure is not accepted (no authentication).
  if (msg.client_id != client_duid_ || msg.transaction_id != active_tx_id_)
    return;
  if (msg.server_id.empty()) return;

  if (state_ == Dhcpv6State::Soliciting &&
      msg.type == Dhcpv6MessageType::Advertise) {
    if (msg.status_code.value_or(0) != 0) return;
    const Dhcpv6IaNa* offered_ia = nullptr;
    for (const Dhcpv6IaNa& ia : msg.ia_nas) {
      if (ia.iaid == iaid_ && ia.status_code == 0 && !ia.addresses.empty()) {
        offered_ia = &ia;
        break;
      }
    }
    if (offered_ia == nullptr) return;
    server_duid_ = msg.server_id;
    state_ = Dhcpv6State::Requesting;
    active_tx_id_ = NextTransactionId();
    exchange_start_ = now;
    current_rto_ = kInitialRetransmitTimeout;
    retries_ = 0;
    // Cache the offered IA addresses temporarily in addresses_ as Tentative so
    // Request includes them.
    addresses_.clear();
    for (const Dhcpv6IaAddress& addr : offered_ia->addresses) {
      InterfaceAddress entry;
      entry.address = addr.address;
      entry.prefix_length = kDhcpv6AddressPrefixLength;
      entry.state = AddressState::Tentative;
      entry.origin = AddressOrigin::Dhcpv6;
      addresses_.push_back(entry);
    }
    SendCurrentExchangeMessage(now);
    return;
  }

  if (msg.type != Dhcpv6MessageType::Reply) return;
  if (msg.status_code.value_or(0) != 0) return;

  if (state_ == Dhcpv6State::StatelessRequesting) {
    if (!msg.dns_servers.empty()) dns_servers_ = msg.dns_servers;
    uint32 refresh_sec =
        msg.info_refresh_time_seconds.value_or(kDefaultInformationRefreshSeconds);
    refresh_sec = std::max(refresh_sec, kMinimumInformationRefreshSeconds);
    state_ = Dhcpv6State::StatelessBound;
    retransmit_deadline_.reset();
    refresh_deadline_ = now + std::chrono::seconds(refresh_sec);
    return;
  }

  if (state_ == Dhcpv6State::Requesting || state_ == Dhcpv6State::Renewing ||
      state_ == Dhcpv6State::Rebinding) {
    for (const Dhcpv6IaNa& ia : msg.ia_nas) {
      if (ia.iaid != iaid_) continue;
      if (ia.status_code ==
              static_cast<uint16>(Dhcpv6StatusCode::NoBinding) &&
          (state_ == Dhcpv6State::Renewing ||
           state_ == Dhcpv6State::Rebinding)) {
        state_ = Dhcpv6State::Requesting;
        active_tx_id_ = NextTransactionId();
        exchange_start_ = now;
        current_rto_ = kInitialRetransmitTimeout;
        retries_ = 0;
        SendCurrentExchangeMessage(now);
        return;
      }
      if (ia.status_code != 0) return;
      ApplyStatefulReply(msg, ia, now);
      return;
    }
  }
}

void Dhcpv6Client::OnTimer(std::chrono::steady_clock::time_point now) {
  for (auto it = addresses_.begin(); it != addresses_.end();) {
    if (state_ == Dhcpv6State::Requesting) {
      ++it;
      continue;
    }
    if (now >= it->valid_until) {
      it = addresses_.erase(it);
      continue;
    }
    if (it->state == AddressState::Preferred && now >= it->preferred_until)
      it->state = AddressState::Deprecated;
    ++it;
  }

  if (state_ == Dhcpv6State::StatelessBound && refresh_deadline_.has_value() &&
      now >= *refresh_deadline_) {
    StartStateless(now);
    return;
  }

  if (state_ == Dhcpv6State::Bound && t1_deadline_.has_value() &&
      now >= *t1_deadline_) {
    state_ = Dhcpv6State::Renewing;
    active_tx_id_ = NextTransactionId();
    exchange_start_ = now;
    current_rto_ = kRenewInitialRetransmitTimeout;
    retries_ = 0;
    SendCurrentExchangeMessage(now);
    return;
  }

  if (state_ == Dhcpv6State::Renewing && t2_deadline_.has_value() &&
      now >= *t2_deadline_) {
    state_ = Dhcpv6State::Rebinding;
    active_tx_id_ = NextTransactionId();
    exchange_start_ = now;
    current_rto_ = kRenewInitialRetransmitTimeout;
    retries_ = 0;
    SendCurrentExchangeMessage(now);
    return;
  }

  if (state_ == Dhcpv6State::Rebinding && addresses_.empty()) {
    StartStateful(now);
    return;
  }

  if (retransmit_deadline_.has_value() && now >= *retransmit_deadline_) {
    if (state_ == Dhcpv6State::Requesting && retries_ >= kMaxRequestRetries) {
      StartStateful(now);
      return;
    }
    retries_++;
    auto max_rto = (state_ == Dhcpv6State::Requesting)
                       ? kMaxRequestRetransmitTimeout
                       : kMaxSolicitRetransmitTimeout;
    current_rto_ = std::min(current_rto_ * 2, max_rto);
    SendCurrentExchangeMessage(now);
  }
}

std::optional<std::chrono::steady_clock::time_point>
Dhcpv6Client::NextDeadline() const {
  std::optional<std::chrono::steady_clock::time_point> earliest =
      retransmit_deadline_;
  auto consider = [&](std::optional<std::chrono::steady_clock::time_point> t) {
    if (!t.has_value()) return;
    if (!earliest.has_value() || *t < *earliest) earliest = *t;
  };
  consider(t1_deadline_);
  consider(t2_deadline_);
  consider(refresh_deadline_);
  return earliest;
}

uint32 Dhcpv6Client::NextTransactionId() {
  if (tx_id_fn_) return tx_id_fn_() & 0xFFFFFFu;
  fallback_tx_id_ = (fallback_tx_id_ + 1) & 0xFFFFFFu;
  return fallback_tx_id_;
}

void Dhcpv6Client::SendCurrentExchangeMessage(
    std::chrono::steady_clock::time_point now) {
  retransmit_deadline_ = now + current_rto_;
  if (!send_fn_) return;

  Dhcpv6Message msg;
  msg.transaction_id = active_tx_id_;
  msg.client_id = client_duid_;
  auto elapsed_cs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - exchange_start_)
                        .count() /
                    10;
  msg.elapsed_time_centiseconds = static_cast<uint16>(
      std::min<int64>(std::max<int64>(elapsed_cs, 0), kMaxElapsedCentiseconds));

  msg.requested_options = {
      static_cast<uint16>(Dhcpv6OptionCode::DnsServers),
      static_cast<uint16>(Dhcpv6OptionCode::DomainList),
  };

  switch (state_) {
    case Dhcpv6State::StatelessRequesting:
      msg.type = Dhcpv6MessageType::InformationRequest;
      msg.requested_options.push_back(
          static_cast<uint16>(Dhcpv6OptionCode::InformationRefreshTime));
      break;
    case Dhcpv6State::Soliciting: {
      msg.type = Dhcpv6MessageType::Solicit;
      Dhcpv6IaNa ia;
      ia.iaid = iaid_;
      msg.ia_nas.push_back(ia);
      break;
    }
    case Dhcpv6State::Requesting:
    case Dhcpv6State::Renewing:
    case Dhcpv6State::Rebinding: {
      if (state_ == Dhcpv6State::Requesting) {
        msg.type = Dhcpv6MessageType::Request;
        msg.server_id = server_duid_;
      } else if (state_ == Dhcpv6State::Renewing) {
        msg.type = Dhcpv6MessageType::Renew;
        msg.server_id = server_duid_;
      } else {
        msg.type = Dhcpv6MessageType::Rebind;
      }
      Dhcpv6IaNa ia;
      ia.iaid = iaid_;
      for (const InterfaceAddress& entry : addresses_) {
        Dhcpv6IaAddress ia_addr;
        ia_addr.address = entry.address;
        ia.addresses.push_back(ia_addr);
      }
      msg.ia_nas.push_back(std::move(ia));
      break;
    }
    default:
      return;
  }

  send_fn_(BuildDhcpv6Message(msg));
}

void Dhcpv6Client::ApplyStatefulReply(
    const Dhcpv6Message& reply, const Dhcpv6IaNa& ia,
    std::chrono::steady_clock::time_point now) {
  if (!reply.dns_servers.empty()) dns_servers_ = reply.dns_servers;
  server_duid_ = reply.server_id;

  addresses_.clear();
  uint32 shortest_preferred = 0xFFFFFFFFu;
  for (const Dhcpv6IaAddress& addr : ia.addresses) {
    if (addr.valid_lifetime_seconds == 0 ||
        addr.preferred_lifetime_seconds > addr.valid_lifetime_seconds)
      continue;
    InterfaceAddress entry;
    entry.address = addr.address;
    entry.prefix_length = kDhcpv6AddressPrefixLength;
    entry.state = (addr.preferred_lifetime_seconds > 0)
                      ? AddressState::Preferred
                      : AddressState::Deprecated;
    entry.origin = AddressOrigin::Dhcpv6;
    entry.preferred_until =
        (addr.preferred_lifetime_seconds == 0xFFFFFFFFu)
            ? std::chrono::steady_clock::time_point::max()
            : (now + std::chrono::seconds(addr.preferred_lifetime_seconds));
    entry.valid_until =
        (addr.valid_lifetime_seconds == 0xFFFFFFFFu)
            ? std::chrono::steady_clock::time_point::max()
            : (now + std::chrono::seconds(addr.valid_lifetime_seconds));
    addresses_.push_back(entry);
    if (addr.preferred_lifetime_seconds > 0 &&
        addr.preferred_lifetime_seconds < shortest_preferred)
      shortest_preferred = addr.preferred_lifetime_seconds;
  }

  if (addresses_.empty()) return;

  uint32 t1 = ia.t1_seconds;
  uint32 t2 = ia.t2_seconds;
  if (t1 == 0 && shortest_preferred != 0xFFFFFFFFu) {
    t1 = (shortest_preferred * kDefaultT1Numerator) /
         kDefaultTimeFractionDenominator;
  }
  if (t2 == 0 && shortest_preferred != 0xFFFFFFFFu) {
    t2 = (shortest_preferred * kDefaultT2Numerator) /
         kDefaultTimeFractionDenominator;
  }

  state_ = Dhcpv6State::Bound;
  retransmit_deadline_.reset();
  if (t1 > 0 && t1 != 0xFFFFFFFFu) t1_deadline_ = now + std::chrono::seconds(t1);
  if (t2 > 0 && t2 != 0xFFFFFFFFu) t2_deadline_ = now + std::chrono::seconds(t2);
}
