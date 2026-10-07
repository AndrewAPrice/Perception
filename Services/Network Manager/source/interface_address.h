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

#include <chrono>

#include "perception/network/ip_address.h"

// Lifecycle of an assigned address (RFC 4862).
enum class AddressState { Tentative, Preferred, Deprecated, Duplicate };

// How an address was configured.
enum class AddressOrigin {
  Static,
  LinkLocal,
  Slaac,
  Dhcpv6,
  Temporary,
  Dhcpv4,
};

// An address assigned to an interface.
struct InterfaceAddress {
  // The assigned address.
  ::perception::network::IpAddress address;
  // On-link prefix length.
  uint8 prefix_length = 0;
  // Current RFC 4862 state.
  AddressState state = AddressState::Tentative;
  // Configuration source.
  AddressOrigin origin = AddressOrigin::Static;
  // End of the preferred lifetime (time_point::max() if infinite).
  std::chrono::steady_clock::time_point preferred_until =
      std::chrono::steady_clock::time_point::max();
  // End of the valid lifetime (time_point::max() if infinite).
  std::chrono::steady_clock::time_point valid_until =
      std::chrono::steady_clock::time_point::max();

  bool operator==(const InterfaceAddress& other) const = default;
};
