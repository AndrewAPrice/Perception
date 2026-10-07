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

#include "checksum.h"

#include "testing.h"
#include "wire_format.h"

namespace {

using ::perception::network::IpAddress;

// IPv4 protocol number for TCP.
constexpr uint8 kProtocolTcp = 6;

// IPv4/IPv6 protocol number for UDP.
constexpr uint8 kProtocolUdp = 17;

// IPv6 Next Header value for ICMPv6.
constexpr uint8 kProtocolIcmpv6 = 58;

TEST(Checksum_InternetChecksumIpv4HeaderVector) {
  // Standard 20-byte IPv4 header from RFC 1071 test vector:
  // 4500 0073 0000 4000 4011 [0000] c0a8 0001 c0a8 00c7
  // Expected checksum: 0xB861.
  const uint8 header_without_checksum[] = {
      0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11,
      0x00, 0x00, 0xC0, 0xA8, 0x00, 0x01, 0xC0, 0xA8, 0x00, 0xC7};
  std::string buf(reinterpret_cast<const char*>(header_without_checksum),
                  sizeof(header_without_checksum));

  uint16 sum = InternetChecksum(buf);
  EXPECT(static_cast<uint16>(0xB861), sum);

  WireWriter writer(buf);
  writer.PatchU16(10, sum);
  EXPECT(static_cast<uint16>(0), InternetChecksum(buf));
}

TEST(Checksum_InternetChecksumOddLengthBuffer) {
  std::string data = {0x01, 0x02, 0x03};
  // 0x0102 + 0x0300 = 0x0402 -> ~0x0402 = 0xFBFD
  EXPECT(static_cast<uint16>(0xFBFD), InternetChecksum(data));
}

TEST(Checksum_TransportChecksumIpv4Udp) {
  IpAddress src = IpAddress::V4(192, 168, 0, 1);
  IpAddress dst = IpAddress::V4(192, 168, 0, 199);

  std::string udp_segment;
  WireWriter writer(udp_segment);
  writer.WriteU16(1234);
  writer.WriteU16(53);
  writer.WriteU16(13);
  writer.WriteU16(0);
  writer.WriteBytes("hello");

  uint16 sum = TransportChecksum(src, dst, kProtocolUdp, udp_segment);
  EXPECT(true, sum != 0);

  writer.PatchU16(6, sum);
  EXPECT(static_cast<uint16>(0),
         TransportChecksum(src, dst, kProtocolUdp, udp_segment));
}

TEST(Checksum_TransportChecksumIpv6Icmpv6Echo) {
  IpAddress src = *IpAddress::Parse("fe80::1");
  IpAddress dst = *IpAddress::Parse("fe80::2");

  std::string icmpv6;
  WireWriter writer(icmpv6);
  writer.WriteU8(128);
  writer.WriteU8(0);
  writer.WriteU16(0);
  writer.WriteU16(0x1234);
  writer.WriteU16(0x0001);
  writer.WriteBytes("ping6");

  uint16 sum = TransportChecksum(src, dst, kProtocolIcmpv6, icmpv6);
  EXPECT(true, sum != 0);

  writer.PatchU16(2, sum);
  EXPECT(static_cast<uint16>(0),
         TransportChecksum(src, dst, kProtocolIcmpv6, icmpv6));
}

TEST(Checksum_TransportChecksumRejectsMismatchedFamilies) {
  IpAddress v4 = IpAddress::V4(10, 0, 2, 15);
  IpAddress v6 = *IpAddress::Parse("fe80::1");
  std::string segment(8, '\0');
  EXPECT(static_cast<uint16>(0),
         TransportChecksum(v4, v6, kProtocolTcp, segment));
}

}  // namespace
