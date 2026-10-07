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

#include <string_view>

#include "perception/network/ip_address.h"

// Computes the RFC 1071 one's-complement Internet Checksum over `data` in
// network byte order, starting from `initial_sum` (unfolded sum of big-endian
// 16-bit words). Returns the 16-bit checksum in host byte order (where the
// high byte is the first wire byte and the low byte is the second wire byte).
// Calling InternetChecksum on a buffer whose checksum field is already
// populated on the wire returns 0 when valid.
uint16 InternetChecksum(std::string_view data, uint32 initial_sum = 0);

// Computes the upper-layer transport checksum (TCP, UDP, ICMPv6) over `data`
// prefixed by the IPv4 (12-byte) or IPv6 (40-byte) pseudo-header for `src`,
// `dst`, and `protocol`. Returns the 16-bit checksum in host byte order.
// Returns 0 if `src` and `dst` do not share a valid IPv4 or IPv6 family.
uint16 TransportChecksum(const ::perception::network::IpAddress& src,
                         const ::perception::network::IpAddress& dst,
                         uint8 protocol, std::string_view data);
