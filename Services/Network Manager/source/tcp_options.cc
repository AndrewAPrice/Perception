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

#include "tcp_options.h"

#include <algorithm>

namespace {

// Option kind marking the end of the option list.
constexpr uint8 kOptionEndOfList = 0;

// Option kind for a single padding byte.
constexpr uint8 kOptionNoOperation = 1;

// Option kind for Maximum Segment Size.
constexpr uint8 kOptionMss = 2;

// Total length in bytes of the MSS option.
constexpr uint8 kMssOptionLength = 4;

// Smallest legal length of a multi-byte option (kind + length).
constexpr uint8 kMinimumOptionLength = 2;

// Size of the fixed TCP header.
constexpr size_t kTcpHeaderSize = 20;

// Size of the fixed IPv4 header.
constexpr size_t kIpv4HeaderSize = 20;

// Size of the fixed IPv6 header.
constexpr size_t kIpv6HeaderSize = 40;

// Minimum IPv4 datagram every host must accept, used for the default MSS.
constexpr uint16 kIpv4MinimumMtu = 576;

// Minimum IPv6 link MTU, used for the default MSS.
constexpr uint16 kIpv6MinimumMtu = 1280;

}  // namespace

std::string EncodeMssOption(uint16 mss) {
  std::string option(kMssOptionLength, '\0');
  option[0] = static_cast<char>(kOptionMss);
  option[1] = static_cast<char>(kMssOptionLength);
  option[2] = static_cast<char>(mss >> 8);
  option[3] = static_cast<char>(mss & 0xFF);
  return option;
}

std::optional<TcpOptions> ParseTcpOptions(std::span<const uint8> options) {
  TcpOptions parsed;
  size_t offset = 0;
  while (offset < options.size()) {
    uint8 kind = options[offset];
    if (kind == kOptionEndOfList) break;
    if (kind == kOptionNoOperation) {
      offset++;
      continue;
    }

    if (offset + 1 >= options.size()) return std::nullopt;
    uint8 length = options[offset + 1];
    if (length < kMinimumOptionLength || offset + length > options.size())
      return std::nullopt;

    if (kind == kOptionMss) {
      if (length != kMssOptionLength) return std::nullopt;
      parsed.mss = static_cast<uint16>((options[offset + 2] << 8) |
                                       options[offset + 3]);
    }
    offset += length;
  }
  return parsed;
}

size_t IpHeaderSize(TcpNetworkLayer layer) {
  return layer == TcpNetworkLayer::kIpv6 ? kIpv6HeaderSize : kIpv4HeaderSize;
}

uint16 MssForMtu(uint16 mtu, TcpNetworkLayer layer) {
  size_t overhead = IpHeaderSize(layer) + kTcpHeaderSize;
  return mtu > overhead ? static_cast<uint16>(mtu - overhead) : 0;
}

uint16 EffectiveSendMss(std::optional<uint16> peer_mss, uint16 path_mtu,
                        TcpNetworkLayer layer) {
  uint16 peer = peer_mss.value_or(MssForMtu(
      layer == TcpNetworkLayer::kIpv6 ? kIpv6MinimumMtu : kIpv4MinimumMtu,
      layer));
  return std::min(peer, MssForMtu(path_mtu, layer));
}
