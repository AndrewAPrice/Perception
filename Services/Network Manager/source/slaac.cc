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

#include "slaac.h"

#include <algorithm>

using ::perception::network::IpAddress;

namespace {

// Universal/Local bit in the first byte of an IEEE 802 MAC address.
constexpr uint8 kUniversalLocalBit = 0x02;

// First inserted byte in a Modified EUI-64 identifier.
constexpr uint8 kEui64MidByte0 = 0xFF;

// Second inserted byte in a Modified EUI-64 identifier.
constexpr uint8 kEui64MidByte1 = 0xFE;

// Required prefix length for SLAAC with a 64-bit EUI-64 IID (RFC 4862 §5.5.3).
constexpr uint8 kSlaacPrefixLength = 64;

std::chrono::steady_clock::time_point LifetimeToTimePoint(
    uint32 lifetime_seconds, std::chrono::steady_clock::time_point now) {
  if (lifetime_seconds == kInfiniteIpv6LifetimeSeconds)
    return std::chrono::steady_clock::time_point::max();
  return now + std::chrono::seconds(lifetime_seconds);
}

}  // namespace

std::array<uint8, 8> ModifiedEui64InterfaceIdentifier(
    const HardwareAddress& mac) {
  return {
      static_cast<uint8>(mac[0] ^ kUniversalLocalBit),
      mac[1],
      mac[2],
      kEui64MidByte0,
      kEui64MidByte1,
      mac[3],
      mac[4],
      mac[5],
  };
}

IpAddress LinkLocalAddressFromMac(const HardwareAddress& mac) {
  IpAddress link_local_prefix =
      IpAddress::V6({0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
  return CombinePrefixAndInterfaceIdentifier(
      link_local_prefix, ModifiedEui64InterfaceIdentifier(mac));
}

std::chrono::steady_clock::time_point ComputeUpdatedValidUntil(
    std::chrono::steady_clock::time_point existing_valid_until,
    uint32 advertised_valid_seconds,
    std::chrono::steady_clock::time_point now) {
  if (advertised_valid_seconds == kInfiniteIpv6LifetimeSeconds)
    return std::chrono::steady_clock::time_point::max();

  auto advertised_duration = std::chrono::seconds(advertised_valid_seconds);
  auto two_hours = std::chrono::seconds(kSlaacTwoHoursSeconds);

  bool remaining_is_infinite =
      (existing_valid_until == std::chrono::steady_clock::time_point::max());
  auto remaining_duration =
      (!remaining_is_infinite && existing_valid_until > now)
          ? (existing_valid_until - now)
          : std::chrono::steady_clock::duration::zero();

  if (advertised_valid_seconds > kSlaacTwoHoursSeconds ||
      (!remaining_is_infinite && advertised_duration > remaining_duration))
    return now + advertised_duration;

  if (!remaining_is_infinite && remaining_duration <= two_hours)
    return existing_valid_until;

  return now + two_hours;
}

SlaacController::SlaacController(const HardwareAddress& mac, Ipv6Sink send_sink)
    : mac_(mac),
      eui64_iid_(ModifiedEui64InterfaceIdentifier(mac)),
      send_sink_(std::move(send_sink)) {}

void SlaacController::BringUpLinkLocal(
    std::chrono::steady_clock::time_point now) {
  IpAddress ll = LinkLocalAddressFromMac(mac_);
  if (FindAddress(ll) != nullptr) return;

  InterfaceAddress entry;
  entry.address = ll;
  entry.prefix_length = kSlaacPrefixLength;
  entry.state = AddressState::Tentative;
  entry.origin = AddressOrigin::LinkLocal;
  entry.preferred_until = std::chrono::steady_clock::time_point::max();
  entry.valid_until = std::chrono::steady_clock::time_point::max();
  addresses_.push_back(entry);

  ra_received_ = false;
  rs_sent_ = 0;
  next_rs_deadline_.reset();
  StartDad(ll, now);
}

void SlaacController::AddTentativeAddress(
    const InterfaceAddress& address,
    std::chrono::steady_clock::time_point now) {
  for (InterfaceAddress& existing : addresses_) {
    if (existing.address == address.address) {
      existing = address;
      existing.state = AddressState::Tentative;
      StartDad(address.address, now);
      return;
    }
  }
  InterfaceAddress entry = address;
  entry.state = AddressState::Tentative;
  addresses_.push_back(entry);
  StartDad(entry.address, now);
}

void SlaacController::OnRouterAdvertisement(
    const NdpRouterAdvertisement& ra,
    std::chrono::steady_clock::time_point now) {
  ra_received_ = true;
  next_rs_deadline_.reset();
  if (ra.retrans_timer_ms > 0)
    retrans_timer_ = std::chrono::milliseconds(ra.retrans_timer_ms);
  for (const NdpPrefixInformation& pio : ra.prefixes)
    OnPrefixInformation(pio, now);
}

void SlaacController::OnPrefixInformation(
    const NdpPrefixInformation& pio,
    std::chrono::steady_clock::time_point now) {
  if (!pio.autonomous) return;
  if (pio.prefix.IsLinkLocal() || pio.prefix.IsMulticast()) return;
  if (pio.preferred_lifetime_seconds > pio.valid_lifetime_seconds) return;
  if (pio.prefix_length != kSlaacPrefixLength) return;

  IpAddress candidate =
      CombinePrefixAndInterfaceIdentifier(pio.prefix, eui64_iid_);
  for (InterfaceAddress& entry : addresses_) {
    if (entry.address != candidate) continue;
    entry.preferred_until =
        LifetimeToTimePoint(pio.preferred_lifetime_seconds, now);
    entry.valid_until = ComputeUpdatedValidUntil(
        entry.valid_until, pio.valid_lifetime_seconds, now);
    if (entry.state == AddressState::Preferred && now >= entry.preferred_until)
      entry.state = AddressState::Deprecated;
    else if (entry.state == AddressState::Deprecated &&
             now < entry.preferred_until)
      entry.state = AddressState::Preferred;
    return;
  }

  if (pio.valid_lifetime_seconds == 0) return;

  InterfaceAddress entry;
  entry.address = candidate;
  entry.prefix_length = kSlaacPrefixLength;
  entry.state = AddressState::Tentative;
  entry.origin = AddressOrigin::Slaac;
  entry.preferred_until =
      LifetimeToTimePoint(pio.preferred_lifetime_seconds, now);
  entry.valid_until = LifetimeToTimePoint(pio.valid_lifetime_seconds, now);
  addresses_.push_back(entry);
  StartDad(candidate, now);
}

void SlaacController::OnNeighborSolicitation(
    const Ipv6Header& ipv6_header, const NdpNeighborSolicitation& solicitation,
    std::chrono::steady_clock::time_point now) {
  (void)now;
  for (InterfaceAddress& entry : addresses_) {
    if (entry.address != solicitation.target) continue;
    if (entry.state == AddressState::Tentative) {
      if (ipv6_header.source.IsUnspecified()) {
        entry.state = AddressState::Duplicate;
        dad_deadlines_.erase(entry.address);
      }
      return;
    }
    if (entry.state == AddressState::Preferred ||
        entry.state == AddressState::Deprecated) {
      if (!send_sink_) return;
      bool from_unspecified = ipv6_header.source.IsUnspecified();
      IpAddress reply_dst =
          from_unspecified ? AllNodesMulticastAddress() : ipv6_header.source;
      NdpNeighborAdvertisement na;
      na.router_flag = false;
      na.solicited_flag = !from_unspecified;
      na.override_flag = true;
      na.target = entry.address;
      na.target_mac = mac_;

      Ipv6Datagram datagram;
      datagram.source = entry.address;
      datagram.destination = reply_dst;
      datagram.next_header = static_cast<uint8>(Ipv6NextHeader::Icmpv6);
      datagram.hop_limit = kNdpHopLimit;
      datagram.payload =
          BuildNeighborAdvertisement(entry.address, reply_dst, na);
      send_sink_(std::move(datagram));
      return;
    }
  }
}

void SlaacController::OnNeighborAdvertisement(
    const NdpNeighborAdvertisement& advertisement,
    std::chrono::steady_clock::time_point now) {
  (void)now;
  for (InterfaceAddress& entry : addresses_) {
    if (entry.address == advertisement.target &&
        entry.state == AddressState::Tentative) {
      entry.state = AddressState::Duplicate;
      dad_deadlines_.erase(entry.address);
    }
  }
}

void SlaacController::OnTimer(std::chrono::steady_clock::time_point now) {
  bool link_local_just_preferred = false;
  for (auto it = addresses_.begin(); it != addresses_.end();) {
    if (it->state != AddressState::Duplicate && now >= it->valid_until) {
      dad_deadlines_.erase(it->address);
      it = addresses_.erase(it);
      continue;
    }
    if (it->state == AddressState::Tentative) {
      auto dad_it = dad_deadlines_.find(it->address);
      if (dad_it != dad_deadlines_.end() && now >= dad_it->second) {
        dad_deadlines_.erase(dad_it);
        it->state = (now < it->preferred_until) ? AddressState::Preferred
                                                : AddressState::Deprecated;
        if (it->origin == AddressOrigin::LinkLocal)
          link_local_just_preferred = true;
      }
    } else if (it->state == AddressState::Preferred &&
               now >= it->preferred_until) {
      it->state = AddressState::Deprecated;
    }
    ++it;
  }

  if (link_local_just_preferred && !ra_received_ && rs_sent_ == 0) {
    if (auto ll = UsableLinkLocalAddress()) {
      SendRouterSolicitation(*ll);
      rs_sent_ = 1;
      next_rs_deadline_ = now + kRouterSolicitationInterval;
    }
  } else if (!ra_received_ && next_rs_deadline_.has_value() &&
             now >= *next_rs_deadline_) {
    if (rs_sent_ < kMaxRouterSolicitations) {
      if (auto ll = UsableLinkLocalAddress()) {
        SendRouterSolicitation(*ll);
        rs_sent_++;
        if (rs_sent_ < kMaxRouterSolicitations)
          next_rs_deadline_ = now + kRouterSolicitationInterval;
        else
          next_rs_deadline_.reset();
      }
    } else {
      next_rs_deadline_.reset();
    }
  }
}

const InterfaceAddress* SlaacController::FindAddress(
    const IpAddress& address) const {
  for (const InterfaceAddress& entry : addresses_) {
    if (entry.address == address) return &entry;
  }
  return nullptr;
}

std::optional<IpAddress> SlaacController::UsableLinkLocalAddress() const {
  for (const InterfaceAddress& entry : addresses_) {
    if (entry.origin == AddressOrigin::LinkLocal &&
        (entry.state == AddressState::Preferred ||
         entry.state == AddressState::Deprecated))
      return entry.address;
  }
  return std::nullopt;
}

std::optional<std::chrono::steady_clock::time_point>
SlaacController::NextDeadline() const {
  std::optional<std::chrono::steady_clock::time_point> earliest =
      next_rs_deadline_;
  auto consider = [&](std::chrono::steady_clock::time_point t) {
    if (t == std::chrono::steady_clock::time_point::max()) return;
    if (!earliest.has_value() || t < *earliest) earliest = t;
  };
  for (const auto& [addr, deadline] : dad_deadlines_) consider(deadline);
  for (const InterfaceAddress& entry : addresses_) {
    if (entry.state == AddressState::Preferred) consider(entry.preferred_until);
    if (entry.state != AddressState::Duplicate) consider(entry.valid_until);
  }
  return earliest;
}

void SlaacController::StartDad(const IpAddress& address,
                               std::chrono::steady_clock::time_point now) {
  dad_deadlines_[address] = now + retrans_timer_;
  if (!send_sink_) return;
  IpAddress dst = SolicitedNodeMulticastAddress(address);
  NdpNeighborSolicitation ns;
  ns.target = address;

  Ipv6Datagram datagram;
  datagram.source = IpAddress::V6Any();
  datagram.destination = dst;
  datagram.next_header = static_cast<uint8>(Ipv6NextHeader::Icmpv6);
  datagram.hop_limit = kNdpHopLimit;
  datagram.payload = BuildNeighborSolicitation(IpAddress::V6Any(), dst, ns);
  send_sink_(std::move(datagram));
}

void SlaacController::SendRouterSolicitation(const IpAddress& source) {
  if (!send_sink_) return;
  IpAddress dst = AllRoutersMulticastAddress();
  NdpRouterSolicitation rs;
  rs.source_mac = mac_;

  Ipv6Datagram datagram;
  datagram.source = source;
  datagram.destination = dst;
  datagram.next_header = static_cast<uint8>(Ipv6NextHeader::Icmpv6);
  datagram.hop_limit = kNdpHopLimit;
  datagram.payload = BuildRouterSolicitation(source, dst, rs);
  send_sink_(std::move(datagram));
}
