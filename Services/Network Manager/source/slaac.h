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
#include <chrono>
#include <map>
#include <optional>
#include <vector>

#include "interface_address.h"
#include "ipv6_header.h"
#include "ndp.h"
#include "perception/network/ip_address.h"

// Infinite lifetime sentinel in NDP Prefix Information Options (RFC 4861).
inline constexpr uint32 kInfiniteIpv6LifetimeSeconds = 0xFFFFFFFFu;

// Two-hour threshold (7200 seconds) for RFC 4862 §5.5.3(e) valid-lifetime
// updates.
inline constexpr uint32 kSlaacTwoHoursSeconds = 7200;

// Computes the 64-bit Modified EUI-64 interface identifier from a 48-bit IEEE
// 802 MAC address (flips the universal/local bit and inserts ff:fe).
std::array<uint8, 8> ModifiedEui64InterfaceIdentifier(
    const HardwareAddress& mac);

// Returns the link-local IPv6 address fe80::<EUI-64> for `mac`.
::perception::network::IpAddress LinkLocalAddressFromMac(
    const HardwareAddress& mac);

// Applies the RFC 4862 §5.5.3(e) "two-hour rule" to compute the updated
// `valid_until` timestamp given the current `existing_valid_until`, the
// advertised `advertised_valid_seconds`, and `now`.
std::chrono::steady_clock::time_point ComputeUpdatedValidUntil(
    std::chrono::steady_clock::time_point existing_valid_until,
    uint32 advertised_valid_seconds,
    std::chrono::steady_clock::time_point now);

// Pure RFC 4862 Stateless Address Autoconfiguration (SLAAC) and Duplicate
// Address Detection (DAD) state machine.
class SlaacController {
 public:
  // Default DAD wait / NS retransmit interval (1 second).
  static constexpr auto kDefaultRetransTimer = std::chrono::milliseconds(1000);

  // Interval between Router Solicitations (4 seconds, RFC 4861 §10).
  static constexpr auto kRouterSolicitationInterval =
      std::chrono::milliseconds(4000);

  // Maximum Router Solicitations sent at interface bring-up (3, RFC 4861 §10).
  static constexpr uint8 kMaxRouterSolicitations = 3;

  SlaacController(const HardwareAddress& mac, Ipv6Sink send_sink);

  // Starts interface bring-up at `now`: creates the tentative EUI-64 link-local
  // address, initiates DAD, and schedules Router Solicitations once link-local
  // DAD succeeds.
  void BringUpLinkLocal(std::chrono::steady_clock::time_point now);

  // Adds or updates a tentative address (e.g. from DHCPv6 or RFC 8981
  // temporary address generation) and starts DAD for it at `now`.
  void AddTentativeAddress(const InterfaceAddress& address,
                           std::chrono::steady_clock::time_point now);

  // Processes a Router Advertisement at `now`: stops RS transmissions, updates
  // `retrans_timer` if non-zero, and processes all autonomous PIOs.
  void OnRouterAdvertisement(const NdpRouterAdvertisement& ra,
                             std::chrono::steady_clock::time_point now);

  // Processes a single Prefix Information Option per RFC 4862 §5.5.3.
  void OnPrefixInformation(const NdpPrefixInformation& pio,
                           std::chrono::steady_clock::time_point now);

  // Inspects an incoming Neighbor Solicitation at `now`. A tentative address
  // fails DAD if another node sends a DAD NS (unspecified source) for the same
  // target. If the target matches one of this interface's non-tentative
  // addresses, emits a solicited Neighbor Advertisement reply.
  void OnNeighborSolicitation(const Ipv6Header& ipv6_header,
                              const NdpNeighborSolicitation& solicitation,
                              std::chrono::steady_clock::time_point now);

  // Inspects an incoming Neighbor Advertisement at `now`. Any tentative address
  // matching `advertisement.target` transitions to AddressState::Duplicate.
  void OnNeighborAdvertisement(const NdpNeighborAdvertisement& advertisement,
                               std::chrono::steady_clock::time_point now);

  // Advances DAD timers, RS transmissions, deprecation transitions, and valid
  // lifetime expirations at `now`.
  void OnTimer(std::chrono::steady_clock::time_point now);

  // Returns the current list of interface addresses.
  const std::vector<InterfaceAddress>& addresses() const { return addresses_; }

  // Looks up an assigned address entry by IPv6 address.
  const InterfaceAddress* FindAddress(
      const ::perception::network::IpAddress& address) const;

  // Returns the preferred or deprecated link-local address if DAD has completed
  // on it, or nullopt while still tentative/duplicate.
  std::optional<::perception::network::IpAddress> UsableLinkLocalAddress() const;

  // Returns the earliest timer deadline (DAD, RS, preferred/valid lifetime).
  std::optional<std::chrono::steady_clock::time_point> NextDeadline() const;

 private:
  // Starts DAD for `address` at `now` and emits a DAD Neighbor Solicitation.
  void StartDad(const ::perception::network::IpAddress& address,
                std::chrono::steady_clock::time_point now);

  // Emits a Router Solicitation from `source` to ff02::2.
  void SendRouterSolicitation(const ::perception::network::IpAddress& source);

  // Local Ethernet MAC address.
  HardwareAddress mac_{};
  // Precomputed 64-bit Modified EUI-64 IID.
  std::array<uint8, 8> eui64_iid_{};
  // Sink receiving generated NDP packets.
  Ipv6Sink send_sink_;
  // Retransmission / DAD interval.
  std::chrono::milliseconds retrans_timer_ = kDefaultRetransTimer;
  // Assigned IPv6 addresses on the interface.
  std::vector<InterfaceAddress> addresses_;
  // Completion deadlines for addresses currently in AddressState::Tentative.
  std::map<::perception::network::IpAddress,
           std::chrono::steady_clock::time_point>
      dad_deadlines_;
  // True once at least one Router Advertisement has been received.
  bool ra_received_ = false;
  // Number of Router Solicitations sent so far.
  uint8 rs_sent_ = 0;
  // Deadline for the next Router Solicitation.
  std::optional<std::chrono::steady_clock::time_point> next_rs_deadline_;
};
