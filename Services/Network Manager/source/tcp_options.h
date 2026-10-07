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

#include <optional>
#include <span>
#include <string>

#include "types.h"

// The network layer carrying a TCP connection. Determines header overhead and
// default MSS values.
enum class TcpNetworkLayer { kIpv4, kIpv6 };

// Options parsed from a TCP header.
struct TcpOptions {
  // Maximum Segment Size option (kind 2), if present.
  std::optional<uint16> mss;
};

// Returns the 4-byte MSS option (kind 2, length 4, value) for a SYN or
// SYN-ACK. The TCP data offset must account for the extra 4 bytes.
std::string EncodeMssOption(uint16 mss);

// Parses the options area of a TCP header (the bytes between the 20-byte fixed
// header and the data offset). Unknown options are skipped. Returns nullopt if
// an option is truncated or has an invalid length.
std::optional<TcpOptions> ParseTcpOptions(std::span<const uint8> options);

// Returns the size of the fixed IP header for `layer` (20 or 40 bytes).
size_t IpHeaderSize(TcpNetworkLayer layer);

// Returns the MSS to advertise for a link MTU: MTU minus the IP and TCP fixed
// headers (1460 for IPv4 and 1440 for IPv6 at MTU 1500).
uint16 MssForMtu(uint16 mtu, TcpNetworkLayer layer);

// Returns the effective send MSS: min(peer MSS, path MTU - headers). When the
// peer sent no MSS option, the RFC 9293 §3.7.1 default is used (536 for IPv4,
// 1220 for IPv6).
uint16 EffectiveSendMss(std::optional<uint16> peer_mss, uint16 path_mtu,
                        TcpNetworkLayer layer);
