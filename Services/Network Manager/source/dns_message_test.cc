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

#include "dns_message.h"

#include <chrono>
#include <string>

#include "dns_cache.h"
#include "testing.h"
#include "wire_format.h"

using ::perception::network::IpAddress;

namespace {

// Standard DNS response flags: QR=1, RD=1, RA=1, RCODE=0.
constexpr uint16 kStandardResponseFlags = 0x8180;

// NXDOMAIN DNS response flags: QR=1, RD=1, RA=1, RCODE=3.
constexpr uint16 kNxdomainResponseFlags = 0x8183;

// SERVFAIL DNS response flags: QR=1, RD=1, RA=1, RCODE=2.
constexpr uint16 kServfailResponseFlags = 0x8182;

// Truncated DNS response flags: QR=1, TC=1, RD=1, RA=1, RCODE=0.
constexpr uint16 kTruncatedResponseFlags = 0x8380;

// Internet DNS class (IN).
constexpr uint16 kClassIn = 1;

// Appends an uncompressed DNS name (e.g. "example.com") in wire format.
void WriteDnsName(WireWriter& writer, std::string_view name) {
  while (!name.empty()) {
    size_t dot = name.find('.');
    std::string_view label = name.substr(0, dot);
    writer.WriteU8(static_cast<uint8>(label.size()));
    writer.WriteBytes(label);
    if (dot == std::string_view::npos) break;
    name.remove_prefix(dot + 1);
  }
  writer.WriteU8(0);
}

// Appends a DNS header to `writer`.
void WriteHeader(WireWriter& writer, uint16 id, uint16 flags, uint16 qdcount,
                 uint16 ancount, uint16 nscount = 0, uint16 arcount = 0) {
  writer.WriteU16(id);
  writer.WriteU16(flags);
  writer.WriteU16(qdcount);
  writer.WriteU16(ancount);
  writer.WriteU16(nscount);
  writer.WriteU16(arcount);
}

// Appends a DNS question section to `writer`.
void WriteQuestion(WireWriter& writer, std::string_view name,
                   DnsRecordType type, uint16 qclass = kClassIn) {
  WriteDnsName(writer, name);
  writer.WriteU16(static_cast<uint16>(type));
  writer.WriteU16(qclass);
}

TEST(DnsMessage_CanonicalDnsName) {
  EXPECT(std::string("example.com"), CanonicalDnsName("Example.COM."));
  EXPECT(std::string("sub.example.com"), CanonicalDnsName("SUB.Example.Com"));
  EXPECT(std::string(""), CanonicalDnsName("."));
  EXPECT(std::string(""), CanonicalDnsName(""));
}

TEST(DnsMessage_EncodeQueryAAndAaaa) {
  auto query_a = EncodeDnsQuery(0x1234, "Example.COM.", DnsRecordType::A);
  ASSERT(true, query_a.has_value());

  WireReader reader(*query_a);
  EXPECT(static_cast<uint16>(0x1234), reader.ReadU16());
  EXPECT(static_cast<uint16>(0x0100), reader.ReadU16());  // RD=1
  EXPECT(static_cast<uint16>(1), reader.ReadU16());       // QDCOUNT=1
  EXPECT(static_cast<uint16>(0), reader.ReadU16());       // ANCOUNT=0
  EXPECT(static_cast<uint16>(0), reader.ReadU16());       // NSCOUNT=0
  EXPECT(static_cast<uint16>(0), reader.ReadU16());       // ARCOUNT=0
  EXPECT(static_cast<uint8>(7), reader.ReadU8());
  EXPECT(std::string_view("Example"), reader.ReadBytes(7));
  EXPECT(static_cast<uint8>(3), reader.ReadU8());
  EXPECT(std::string_view("COM"), reader.ReadBytes(3));
  EXPECT(static_cast<uint8>(0), reader.ReadU8());
  EXPECT(static_cast<uint16>(1), reader.ReadU16());  // QTYPE=A
  EXPECT(static_cast<uint16>(1), reader.ReadU16());  // QCLASS=IN
  EXPECT(static_cast<size_t>(0), reader.Remaining());
  EXPECT(true, reader.ok());

  auto query_aaaa = EncodeDnsQuery(0xABCD, "ipv6.google.com", DnsRecordType::Aaaa);
  ASSERT(true, query_aaaa.has_value());
  WireReader reader6(*query_aaaa);
  EXPECT(static_cast<uint16>(0xABCD), reader6.ReadU16());
  reader6.Skip(10 + 1 + 4 + 1 + 6 + 1 + 3 + 1);
  EXPECT(static_cast<uint16>(28), reader6.ReadU16());  // QTYPE=AAAA
  EXPECT(static_cast<uint16>(1), reader6.ReadU16());   // QCLASS=IN
}

TEST(DnsMessage_EncodeQueryRejectsInvalidNames) {
  EXPECT(false, EncodeDnsQuery(1, "", DnsRecordType::A).has_value());
  EXPECT(false, EncodeDnsQuery(1, ".", DnsRecordType::A).has_value());
  EXPECT(false, EncodeDnsQuery(1, "a..b.com", DnsRecordType::A).has_value());
  EXPECT(false, EncodeDnsQuery(1, ".example.com", DnsRecordType::A).has_value());

  std::string long_label(64, 'a');
  EXPECT(false, EncodeDnsQuery(1, long_label + ".com", DnsRecordType::A).has_value());

  std::string max_label(63, 'a');
  std::string too_long_name =
      max_label + "." + max_label + "." + max_label + "." + max_label + ".ab";
  EXPECT(false, EncodeDnsQuery(1, too_long_name, DnsRecordType::A).has_value());
}

TEST(DnsMessage_DecodeARecordWithCompressionPointer) {
  std::string packet;
  WireWriter writer(packet);
  WriteHeader(writer, 0x4242, kStandardResponseFlags, 1, 1);
  WriteQuestion(writer, "example.com", DnsRecordType::A);
  // Answer owner: compression pointer to offset 12 ("example.com").
  writer.WriteU16(0xC00C);
  writer.WriteU16(static_cast<uint16>(DnsRecordType::A));
  writer.WriteU16(kClassIn);
  writer.WriteU32(300);
  writer.WriteU16(4);
  writer.WriteU8(93);
  writer.WriteU8(184);
  writer.WriteU8(216);
  writer.WriteU8(34);

  auto decoded = DecodeDnsResponse(packet);
  ASSERT(true, decoded.has_value());
  EXPECT(static_cast<uint16>(0x4242), decoded->id);
  EXPECT(false, decoded->truncated);
  EXPECT(DnsResponseCode::NoError, decoded->response_code);
  ASSERT(true, decoded->question.has_value());
  EXPECT(std::string("example.com"), decoded->question->name);
  EXPECT(std::string("example.com"), decoded->canonical_name);
  ASSERT(static_cast<size_t>(1), decoded->addresses.size());
  EXPECT(IpAddress::V4(93, 184, 216, 34), decoded->addresses[0].address);
  EXPECT(static_cast<uint32>(300), decoded->addresses[0].ttl);
  EXPECT(true, IsResponseToQuery(*decoded, 0x4242, "EXAMPLE.COM.", DnsRecordType::A));
  EXPECT(false, IsResponseToQuery(*decoded, 0x4242, "example.com", DnsRecordType::Aaaa));
  EXPECT(false, IsResponseToQuery(*decoded, 0x9999, "example.com", DnsRecordType::A));
}

TEST(DnsMessage_DecodeAaaaAndCnameChain) {
  std::string packet;
  WireWriter writer(packet);
  WriteHeader(writer, 0x1111, kStandardResponseFlags, 1, 3);
  WriteQuestion(writer, "www.example.com", DnsRecordType::Aaaa);

  // Record 1: www.example.com (ptr 12) CNAME alias.example.com (TTL 120).
  writer.WriteU16(0xC00C);
  writer.WriteU16(static_cast<uint16>(DnsRecordType::Cname));
  writer.WriteU16(kClassIn);
  writer.WriteU32(120);
  std::string cname1_rdata;
  WireWriter cname1_writer(cname1_rdata);
  WriteDnsName(cname1_writer, "alias.example.com");
  writer.WriteU16(static_cast<uint16>(cname1_rdata.size()));
  writer.WriteBytes(cname1_rdata);

  // Record 2: alias.example.com CNAME target.example.com (TTL 60).
  WriteDnsName(writer, "alias.example.com");
  writer.WriteU16(static_cast<uint16>(DnsRecordType::Cname));
  writer.WriteU16(kClassIn);
  writer.WriteU32(60);
  std::string cname2_rdata;
  WireWriter cname2_writer(cname2_rdata);
  WriteDnsName(cname2_writer, "target.example.com");
  writer.WriteU16(static_cast<uint16>(cname2_rdata.size()));
  writer.WriteBytes(cname2_rdata);

  // Record 3: target.example.com AAAA 2001:db8::1 (TTL 500 -> clamped by chain to 60).
  WriteDnsName(writer, "target.example.com");
  writer.WriteU16(static_cast<uint16>(DnsRecordType::Aaaa));
  writer.WriteU16(kClassIn);
  writer.WriteU32(500);
  writer.WriteU16(16);
  IpAddress v6 = *IpAddress::Parse("2001:db8::1");
  writer.WriteIpv6Address(v6);

  auto decoded = DecodeDnsResponse(packet);
  ASSERT(true, decoded.has_value());
  EXPECT(std::string("target.example.com"), decoded->canonical_name);
  ASSERT(static_cast<size_t>(1), decoded->addresses.size());
  EXPECT(v6, decoded->addresses[0].address);
  EXPECT(static_cast<uint32>(60), decoded->addresses[0].ttl);
}

TEST(DnsMessage_CompressionPointerLoopRejected) {
  std::string packet;
  WireWriter writer(packet);
  WriteHeader(writer, 0x2222, kStandardResponseFlags, 1, 0);
  // Question name at offset 12 points back to offset 12 (infinite loop).
  writer.WriteU16(0xC00C);
  writer.WriteU16(static_cast<uint16>(DnsRecordType::A));
  writer.WriteU16(kClassIn);

  EXPECT(false, DecodeDnsResponse(packet).has_value());
}

TEST(DnsMessage_MutualCompressionPointerCycleRejected) {
  std::string packet;
  WireWriter writer(packet);
  WriteHeader(writer, 0x2223, kStandardResponseFlags, 1, 1);
  // Offset 12: question "a.com" (7 bytes) + type/class (4 bytes) = ends at 23.
  WriteQuestion(writer, "a.com", DnsRecordType::A);
  // Offset 23: answer owner points to offset 25, and offset 25 points back to 23.
  writer.WriteU16(0xC019);
  writer.WriteU16(0xC017);
  writer.WriteU16(static_cast<uint16>(DnsRecordType::A));
  writer.WriteU16(kClassIn);
  writer.WriteU32(60);
  writer.WriteU16(4);
  writer.WriteU32(0x01020304);

  EXPECT(false, DecodeDnsResponse(packet).has_value());
}

TEST(DnsMessage_TruncatedFlagAndSoaNegativeTtl) {
  std::string packet;
  WireWriter writer(packet);
  WriteHeader(writer, 0x3333, kNxdomainResponseFlags, 1, 0, 1, 0);
  WriteQuestion(writer, "missing.example.com", DnsRecordType::A);

  // Authority SOA record with record TTL 900 and SOA MINIMUM 180.
  WriteDnsName(writer, "example.com");
  writer.WriteU16(static_cast<uint16>(DnsRecordType::Soa));
  writer.WriteU16(kClassIn);
  writer.WriteU32(900);
  std::string soa_rdata;
  WireWriter soa_writer(soa_rdata);
  WriteDnsName(soa_writer, "ns1.example.com");
  WriteDnsName(soa_writer, "hostmaster.example.com");
  soa_writer.WriteU32(2026100601);  // SERIAL
  soa_writer.WriteU32(7200);        // REFRESH
  soa_writer.WriteU32(3600);        // RETRY
  soa_writer.WriteU32(1209600);     // EXPIRE
  soa_writer.WriteU32(180);         // MINIMUM
  writer.WriteU16(static_cast<uint16>(soa_rdata.size()));
  writer.WriteBytes(soa_rdata);

  auto decoded = DecodeDnsResponse(packet);
  ASSERT(true, decoded.has_value());
  EXPECT(DnsResponseCode::NameError, decoded->response_code);
  EXPECT(true, decoded->addresses.empty());
  ASSERT(true, decoded->negative_ttl.has_value());
  EXPECT(static_cast<uint32>(180), *decoded->negative_ttl);

  // Truncated response with one valid A record still preserves the record.
  std::string tc_packet;
  WireWriter tc_writer(tc_packet);
  WriteHeader(tc_writer, 0x3334, kTruncatedResponseFlags, 1, 1);
  WriteQuestion(tc_writer, "tc.example.com", DnsRecordType::A);
  tc_writer.WriteU16(0xC00C);
  tc_writer.WriteU16(static_cast<uint16>(DnsRecordType::A));
  tc_writer.WriteU16(kClassIn);
  tc_writer.WriteU32(0x80000001);  // Sign bit set -> treated as TTL 0.
  tc_writer.WriteU16(4);
  tc_writer.WriteU8(1);
  tc_writer.WriteU8(2);
  tc_writer.WriteU8(3);
  tc_writer.WriteU8(4);

  auto tc_decoded = DecodeDnsResponse(tc_packet);
  ASSERT(true, tc_decoded.has_value());
  EXPECT(true, tc_decoded->truncated);
  ASSERT(static_cast<size_t>(1), tc_decoded->addresses.size());
  EXPECT(static_cast<uint32>(0), tc_decoded->addresses[0].ttl);
}

TEST(DnsMessage_MalformedPacketsRejected) {
  // Too short for header.
  EXPECT(false, DecodeDnsResponse("short").has_value());

  // Query packet (QR=0) rejected by response decoder.
  auto query = EncodeDnsQuery(1, "example.com", DnsRecordType::A);
  ASSERT(true, query.has_value());
  EXPECT(false, DecodeDnsResponse(*query).has_value());

  // RDLENGTH exceeds remaining buffer.
  std::string bad_rdlen;
  WireWriter writer(bad_rdlen);
  WriteHeader(writer, 1, kStandardResponseFlags, 1, 1);
  WriteQuestion(writer, "example.com", DnsRecordType::A);
  writer.WriteU16(0xC00C);
  writer.WriteU16(static_cast<uint16>(DnsRecordType::A));
  writer.WriteU16(kClassIn);
  writer.WriteU32(60);
  writer.WriteU16(4);
  writer.WriteU8(1);  // Only 1 byte instead of 4.
  EXPECT(false, DecodeDnsResponse(bad_rdlen).has_value());

  // A record with wrong RDLENGTH (5 bytes).
  std::string wrong_a_size;
  WireWriter writer2(wrong_a_size);
  WriteHeader(writer2, 1, kStandardResponseFlags, 1, 1);
  WriteQuestion(writer2, "example.com", DnsRecordType::A);
  writer2.WriteU16(0xC00C);
  writer2.WriteU16(static_cast<uint16>(DnsRecordType::A));
  writer2.WriteU16(kClassIn);
  writer2.WriteU32(60);
  writer2.WriteU16(5);
  writer2.WriteZeros(5);
  EXPECT(false, DecodeDnsResponse(wrong_a_size).has_value());
}

TEST(DnsCache_PositiveAndNegativeCachingAndPurge) {
  DnsCache cache;
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(1000));

  // Positive response with TTL=1 (clamped to 5s) and TTL=200000 (clamped to 86400s).
  DnsResponse pos;
  pos.response_code = DnsResponseCode::NoError;
  pos.addresses.push_back({IpAddress::V4(1, 1, 1, 1), 1});
  pos.addresses.push_back({IpAddress::V4(1, 0, 0, 1), 200000});
  EXPECT(true, cache.Store("Example.com.", DnsRecordType::A, pos, t0));

  auto hit = cache.Lookup("example.com", DnsRecordType::A, t0 + std::chrono::seconds(3));
  ASSERT(true, hit.has_value());
  EXPECT(false, hit->negative);
  ASSERT(static_cast<size_t>(2), hit->addresses.size());
  EXPECT(static_cast<uint32>(2), hit->addresses[0].ttl);
  EXPECT(static_cast<uint32>(86397), hit->addresses[1].ttl);

  // At t0 + 5s, the first record has expired, leaving only the second.
  auto hit_later =
      cache.Lookup("example.com", DnsRecordType::A, t0 + std::chrono::seconds(5));
  ASSERT(true, hit_later.has_value());
  ASSERT(static_cast<size_t>(1), hit_later->addresses.size());
  EXPECT(IpAddress::V4(1, 0, 0, 1), hit_later->addresses[0].address);

  // Negative response without SOA caches for 30s.
  DnsResponse neg;
  neg.response_code = DnsResponseCode::NameError;
  EXPECT(true, cache.Store("example.com", DnsRecordType::Aaaa, neg, t0));

  auto neg_hit =
      cache.Lookup("example.com", DnsRecordType::Aaaa, t0 + std::chrono::seconds(29));
  ASSERT(true, neg_hit.has_value());
  EXPECT(true, neg_hit->negative);
  EXPECT(true, neg_hit->addresses.empty());
  EXPECT(false,
         cache.Lookup("example.com", DnsRecordType::Aaaa, t0 + std::chrono::seconds(30))
             .has_value());

  // SERVFAIL is not cached.
  DnsResponse servfail;
  servfail.response_code = DnsResponseCode::ServerFailure;
  EXPECT(false, cache.Store("fail.example.com", DnsRecordType::A, servfail, t0));

  // Purge removes expired negative entry while keeping the 1-day positive entry.
  EXPECT(static_cast<size_t>(2), cache.Size());
  cache.Purge(t0 + std::chrono::seconds(30));
  EXPECT(static_cast<size_t>(1), cache.Size());
}

}  // namespace
