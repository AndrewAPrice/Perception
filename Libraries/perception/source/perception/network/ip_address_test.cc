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

#include "perception/network/ip_address.h"

#include <map>
#include <string>
#include <vector>

#include "perception/serialization/binary_deserializer.h"
#include "perception/serialization/binary_serializer.h"
#include "perception/serialization/memory_read_stream.h"
#include "perception/serialization/vector_write_stream.h"
#include "testing.h"

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

namespace {

// Parses `text` and formats it again, or returns "<invalid>".
std::string RoundTrip(std::string_view text) {
  auto address = IpAddress::Parse(text);
  if (!address) return "<invalid>";
  return address->ToString();
}

}  // namespace

TEST(IpAddressParsesIpv4) {
  auto address = IpAddress::Parse("10.0.2.15");
  EXPECT(true, address.has_value());
  EXPECT(true, address->IsV4());
  EXPECT(true, *address == IpAddress::V4(10, 0, 2, 15));
  EXPECT((size_t)4, address->Length());
  EXPECT(std::string("10.0.2.15"), address->ToString());
  EXPECT(std::string("0.0.0.0"), RoundTrip("0.0.0.0"));
  EXPECT(std::string("255.255.255.255"), RoundTrip("255.255.255.255"));
}

TEST(IpAddressRejectsInvalidIpv4) {
  for (const char* text :
       {"", "1", "1.2.3", "1.2.3.4.5", "256.1.1.1", "1..2.3", "1.2.3.", ".1.2.3",
        "1.2.3.a", "1234.1.1.1", " 1.2.3.4", "[1.2.3.4]", "google.com"}) {
    EXPECT(false, IpAddress::Parse(text).has_value());
  }
}

TEST(IpAddressFormatsRfc5952) {
  EXPECT(std::string("2001:db8::1"), RoundTrip("2001:0DB8:0000:0000:0000:0000:0000:0001"));
  EXPECT(std::string("::"), RoundTrip("::"));
  EXPECT(std::string("::1"), RoundTrip("::1"));
  EXPECT(std::string("1::"), RoundTrip("1::"));
  EXPECT(std::string("fe80::5054:ff:fe12:3456"), RoundTrip("fe80::5054:ff:fe12:3456"));
  // A single zero group is not compressed.
  EXPECT(std::string("2001:db8:0:1:1:1:1:1"), RoundTrip("2001:db8:0:1:1:1:1:1"));
  // The longest run is compressed.
  EXPECT(std::string("2001:0:0:1::1"), RoundTrip("2001:0:0:1:0:0:0:1"));
  // The first run wins a tie.
  EXPECT(std::string("2001:db8::1:0:0:1"), RoundTrip("2001:db8:0:0:1:0:0:1"));
  EXPECT(std::string("fec0::3"), RoundTrip("fec0:0:0:0:0:0:0:3"));
  EXPECT(std::string("1:2:3:4:5:6:7:8"), RoundTrip("1:2:3:4:5:6:7:8"));
  EXPECT(std::string("0:2:3:4:5:6:7:8"), RoundTrip("::2:3:4:5:6:7:8"));
  EXPECT(std::string("::102:304"), RoundTrip("::1.2.3.4"));
  EXPECT(std::string("64:ff9b::102:304"), RoundTrip("64:ff9b::1.2.3.4"));
}

TEST(IpAddressParsesBrackets) {
  auto address = IpAddress::Parse("[2001:4860:4860::8888]");
  EXPECT(true, address.has_value());
  EXPECT(true, address->IsV6());
  EXPECT(std::string("2001:4860:4860::8888"), address->ToString());
  EXPECT(false, IpAddress::Parse("[::1").has_value());
  EXPECT(false, IpAddress::Parse("::1]").has_value());
  EXPECT(false, IpAddress::Parse("[]").has_value());
  EXPECT(false, IpAddress::Parse("[").has_value());
}

TEST(IpAddressNormalizesV4Mapped) {
  auto address = IpAddress::Parse("::ffff:10.0.2.2");
  EXPECT(true, address.has_value());
  EXPECT(true, address->IsV4());
  EXPECT(true, *address == IpAddress::V4(10, 0, 2, 2));
  EXPECT(true, *IpAddress::Parse("[::ffff:a00:202]") == IpAddress::V4(10, 0, 2, 2));

  std::array<uint8, 16> mapped{};
  mapped[10] = 0xFF;
  mapped[11] = 0xFF;
  mapped[12] = 1;
  mapped[13] = 2;
  mapped[14] = 3;
  mapped[15] = 4;
  EXPECT(std::string("::ffff:1.2.3.4"), IpAddress::V6(mapped).ToString());
}

TEST(IpAddressRejectsInvalidIpv6) {
  for (const char* text :
       {":", ":::", "1:::2", "1::2::3", "1:2:3:4:5:6:7", "1:2:3:4:5:6:7:8:9",
        "12345::", "g::", ":1::", "1::2:", "1:2:3:4:5:6:7:8::",
        "::1.2.3.4:5", "1.2.3.4::", "fe80::1%eth0", "1:2:3:4:5:6:7:1.2.3.4",
        "::1.2.3"}) {
    EXPECT(false, IpAddress::Parse(text).has_value());
  }
  // "::" in a full eight-group address must still stand for a zero group.
  EXPECT(true, IpAddress::Parse("1:2:3:4:5:6::7").has_value());
  EXPECT(true, IpAddress::Parse("1:2:3:4:5:6:1.2.3.4").has_value());
}

TEST(IpAddressClassifiers) {
  EXPECT(true, IpAddress().IsUnspecified());
  EXPECT(IpAddressFamily::Unspecified, IpAddress().family());
  EXPECT(std::string(""), IpAddress().ToString());
  EXPECT(true, IpAddress::V4Any().IsUnspecified());
  EXPECT(true, IpAddress::V6Any().IsUnspecified());
  EXPECT(false, IpAddress::V4(0, 0, 0, 1).IsUnspecified());

  EXPECT(true, IpAddress::V4(127, 1, 2, 3).IsLoopback());
  EXPECT(true, IpAddress::Parse("::1")->IsLoopback());
  EXPECT(false, IpAddress::Parse("::2")->IsLoopback());

  EXPECT(true, IpAddress::V4(169, 254, 1, 1).IsLinkLocal());
  EXPECT(true, IpAddress::Parse("fe80::2")->IsLinkLocal());
  EXPECT(true, IpAddress::Parse("febf::2")->IsLinkLocal());
  EXPECT(false, IpAddress::Parse("fec0::2")->IsLinkLocal());

  EXPECT(true, IpAddress::V4(224, 0, 0, 1).IsMulticast());
  EXPECT(true, IpAddress::V4(239, 255, 255, 250).IsMulticast());
  EXPECT(false, IpAddress::V4(240, 0, 0, 1).IsMulticast());
  EXPECT(true, IpAddress::Parse("ff02::1:ff12:3456")->IsMulticast());
  EXPECT(false, IpAddress::Parse("fe80::1")->IsMulticast());

  EXPECT(true, IpAddress::V4Broadcast().IsBroadcast());
  EXPECT(false, IpAddress::V4(10, 0, 2, 255).IsBroadcast());
}

TEST(IpAddressPrefixes) {
  IpAddress subnet = IpAddress::V4(10, 0, 2, 0);
  EXPECT(true, IpAddress::V4(10, 0, 2, 3).IsInPrefix(subnet, 24));
  EXPECT(false, IpAddress::V4(10, 0, 3, 3).IsInPrefix(subnet, 24));
  EXPECT(true, IpAddress::V4(8, 8, 8, 8).IsInPrefix(subnet, 0));
  EXPECT(false, IpAddress::Parse("fec0::1")->IsInPrefix(subnet, 0));
  EXPECT(true, IpAddress::Parse("fec0::5054:ff:fe12:3456")
                   ->IsInPrefix(*IpAddress::Parse("fec0::"), 64));
  EXPECT(false, IpAddress::Parse("fec1::1")
                    ->IsInPrefix(*IpAddress::Parse("fec0::"), 64));
  EXPECT(true, IpAddress::Parse("fec1::1")
                   ->IsInPrefix(*IpAddress::Parse("fec0::"), 15));
  EXPECT(true, IpAddress::V4(1, 2, 3, 4).IsInPrefix(IpAddress::V4(1, 2, 3, 4), 200));

  EXPECT((uint8)32, IpAddress::V4(1, 2, 3, 4).CommonPrefixLength(IpAddress::V4(1, 2, 3, 4)));
  EXPECT((uint8)23, IpAddress::V4(10, 0, 2, 0).CommonPrefixLength(IpAddress::V4(10, 0, 3, 0)));
  EXPECT((uint8)0, IpAddress::V4(0, 0, 0, 0).CommonPrefixLength(IpAddress::V6Any()));
  EXPECT((uint8)128, IpAddress::V6Any().CommonPrefixLength(IpAddress::V6Any()));
}

TEST(IpAddressOrderingAndBytes) {
  IpAddress a = IpAddress::V4(10, 0, 2, 2);
  IpAddress b = IpAddress::V4(10, 0, 2, 3);
  IpAddress c = *IpAddress::Parse("::1");
  EXPECT(true, a < b);
  EXPECT(true, b < c);
  EXPECT(true, a != b);
  std::map<IpAddress, int> map;
  map[a] = 1;
  map[IpAddress::V4(10, 0, 2, 2)] = 2;
  EXPECT((size_t)1, map.size());

  const uint8 wire[] = {192, 168, 1, 7, 99};
  IpAddress from_wire = IpAddress::FromBytes(IpAddressFamily::V4, wire);
  EXPECT(true, from_wire == IpAddress::V4(192, 168, 1, 7));
  EXPECT(true, IpAddress::FromBytes(IpAddressFamily::V6, wire).family() ==
                   IpAddressFamily::Unspecified);

  uint8 out[4] = {};
  from_wire.CopyTo(out);
  EXPECT(192, (int)out[0]);
  EXPECT(7, (int)out[3]);
}

TEST(IpAddressSerializationRoundTrip) {
  for (const IpAddress& original :
       {IpAddress(), IpAddress::V4(10, 0, 2, 15),
        *IpAddress::Parse("2001:db8:1234:5678:9abc:def0:1122:3344")}) {
    IpAddress copy = original;
    std::vector<std::byte> serialized =
        ::perception::serialization::SerializeToByteVector(copy);
    IpAddress deserialized = IpAddress::V4(1, 1, 1, 1);
    ::perception::serialization::DeserializeFromByteVector(deserialized,
                                                           serialized);
    EXPECT(true, deserialized == original);
    EXPECT(original.ToString(), deserialized.ToString());
  }
}
