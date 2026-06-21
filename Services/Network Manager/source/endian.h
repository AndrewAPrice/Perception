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

#include "perception/network/network_service.h"

// Swaps the byte order of a 16-bit value.
inline uint16 Swap16BitEndian(uint16 val) { return (val << 8) | (val >> 8); }

// Swaps the byte order of a 32-bit value.
inline uint32 Swap32BitEndian(uint32 val) {
  return ((val & 0xFF000000) >> 24) | ((val & 0x00FF0000) >> 8) |
         ((val & 0x0000FF00) << 8) | ((val & 0x000000FF) << 24);
}

// Converts a structured IpAddress into a raw 32-bit
// integer in host-endianness representation.
inline uint32 IpToUint32(const ::perception::network::IpAddress& ip) {
  return (uint32)ip.address[0] | ((uint32)ip.address[1] << 8) |
         ((uint32)ip.address[2] << 16) | ((uint32)ip.address[3] << 24);
}

// Converts a raw 32-bit integer in host-endianness representation back into
// a structured IpAddress.
inline ::perception::network::IpAddress Uint32ToIp(uint32 ip_val) {
  ::perception::network::IpAddress ip;
  ip.address[0] = ip_val & 0xFF;
  ip.address[1] = (ip_val >> 8) & 0xFF;
  ip.address[2] = (ip_val >> 16) & 0xFF;
  ip.address[3] = (ip_val >> 24) & 0xFF;
  return ip;
}
