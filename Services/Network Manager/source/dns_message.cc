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

#include <algorithm>
#include <array>
#include <span>

namespace {

// Size of the fixed DNS header.
constexpr size_t kHeaderSize = 12;

// Header flag: the message is a response.
constexpr uint16 kResponseFlag = 0x8000;

// Header flag: the message was truncated.
constexpr uint16 kTruncatedFlag = 0x0200;

// Header flag: recursion desired.
constexpr uint16 kRecursionDesiredFlag = 0x0100;

// Mask of the RCODE bits in the header flags.
constexpr uint16 kResponseCodeMask = 0x000F;

// The Internet (IN) class.
constexpr uint16 kClassInternet = 1;

// Maximum number of compression pointers followed while reading one name.
constexpr int kMaxCompressionJumps = 16;

// Maximum encoded length of a domain name, including the root label.
constexpr size_t kMaxEncodedNameLength = 255;

// Maximum decoded domain name length in bytes.
constexpr size_t kMaxDomainNameLength = 255;

// Maximum length of a single label.
constexpr size_t kMaxLabelLength = 63;

// Top two bits of a length byte that mark a compression pointer.
constexpr uint8 kPointerBits = 0xC0;

// Maximum number of CNAME links followed from the question name.
constexpr int kMaxCnameChainLength = 16;

// TTLs with this bit set are treated as zero (RFC 2181 section 8).
constexpr uint32 kTtlSignBit = 0x80000000;

// Size of the fixed fields (type, class, TTL, RDLENGTH) of a resource record.
constexpr size_t kRecordFixedSize = 10;

// Size of the five 32-bit fields that end an SOA record.
constexpr size_t kSoaFixedFieldsSize = 20;

// Size of the question type and class fields.
constexpr size_t kQuestionFixedSize = 4;

// Number of bytes in an A record.
constexpr size_t kARecordSize = 4;

// Number of bytes in an AAAA record.
constexpr size_t kAaaaRecordSize = 16;

// A bounds-checked reader over a DNS message.
class MessageReader {
 public:
  explicit MessageReader(std::string_view packet) : packet_(packet) {}

  // Reads a big-endian 16-bit value at `offset`.
  std::optional<uint16> ReadUint16(size_t offset) const {
    if (offset + 2 > packet_.size()) return std::nullopt;
    return static_cast<uint16>((Byte(offset) << 8) | Byte(offset + 1));
  }

  // Reads a big-endian 32-bit value at `offset`.
  std::optional<uint32> ReadUint32(size_t offset) const {
    if (offset + 4 > packet_.size()) return std::nullopt;
    return (static_cast<uint32>(Byte(offset)) << 24) |
           (static_cast<uint32>(Byte(offset + 1)) << 16) |
           (static_cast<uint32>(Byte(offset + 2)) << 8) |
           static_cast<uint32>(Byte(offset + 3));
  }

  // Reads a possibly compressed name starting at `offset` into `name`
  // (canonical form) and advances `offset` past the name in place.
  bool ReadName(size_t& offset, std::string& name) const {
    name.clear();
    size_t position = offset;
    size_t encoded_length = 0;
    int jumps = 0;
    bool jumped = false;
    while (true) {
      if (position >= packet_.size()) return false;
      uint8 length = Byte(position);
      if ((length & kPointerBits) == kPointerBits) {
        if (position + 1 >= packet_.size()) return false;
        if (++jumps > kMaxCompressionJumps) return false;
        size_t target = (static_cast<size_t>(length & ~kPointerBits) << 8) |
                        Byte(position + 1);
        if (!jumped) offset = position + 2;
        jumped = true;
        position = target;
        continue;
      }
      // The 0x40 and 0x80 label types are reserved/obsolete.
      if ((length & kPointerBits) != 0) return false;
      encoded_length += length + 1;
      if (encoded_length > kMaxEncodedNameLength) return false;
      if (length == 0) {
        if (!jumped) offset = position + 1;
        return true;
      }
      if (position + 1 + length > packet_.size()) return false;
      const size_t added = (name.empty() ? 0 : 1) + length;
      if (name.size() + added > kMaxDomainNameLength) return false;
      if (!name.empty()) name.push_back('.');
      for (size_t i = 0; i < length; i++) {
        char c = static_cast<char>(Byte(position + 1 + i));
        // A dot inside a label would make names ambiguous.
        if (c == '.') return false;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        name.push_back(c);
      }
      position += 1 + length;
    }
  }

  // Returns the bytes in [offset, offset + length), or nullopt if out of
  // bounds.
  std::optional<std::string_view> Slice(size_t offset, size_t length) const {
    if (offset > packet_.size() || length > packet_.size() - offset)
      return std::nullopt;
    return packet_.substr(offset, length);
  }

 private:
  // Returns the byte at `offset`; callers have checked bounds.
  uint8 Byte(size_t offset) const {
    return static_cast<uint8>(packet_[offset]);
  }

  // The full message.
  std::string_view packet_;
};

// A parsed resource record header.
struct ResourceRecord {
  // Canonical owner name.
  std::string owner;
  // Record type.
  uint16 type = 0;
  // Record class.
  uint16 record_class = 0;
  // TTL with the sign bit treated as zero.
  uint32 ttl = 0;
  // Offset of RDATA in the message.
  size_t data_offset = 0;
  // Length of RDATA.
  uint16 data_length = 0;
};

// Reads one resource record at `offset` and advances past it.
std::optional<ResourceRecord> ReadRecord(const MessageReader& reader,
                                         size_t& offset) {
  ResourceRecord record;
  if (!reader.ReadName(offset, record.owner)) return std::nullopt;
  auto type = reader.ReadUint16(offset);
  auto record_class = reader.ReadUint16(offset + 2);
  auto ttl = reader.ReadUint32(offset + 4);
  auto data_length = reader.ReadUint16(offset + 8);
  if (!type || !record_class || !ttl || !data_length) return std::nullopt;
  record.type = *type;
  record.record_class = *record_class;
  record.ttl = (*ttl & kTtlSignBit) ? 0 : *ttl;
  record.data_offset = offset + kRecordFixedSize;
  record.data_length = *data_length;
  if (!reader.Slice(record.data_offset, record.data_length))
    return std::nullopt;
  offset = record.data_offset + record.data_length;
  return record;
}

// Reads a name that must start inside the RDATA of `record`.
bool ReadNameInRecordData(const MessageReader& reader,
                          const ResourceRecord& record, size_t& offset,
                          std::string& name) {
  if (offset >= record.data_offset + record.data_length) return false;
  if (!reader.ReadName(offset, name)) return false;
  return offset <= record.data_offset + record.data_length;
}

// Writes `value` as big-endian bytes into `out`.
void AppendUint16(std::string& out, uint16 value) {
  out.push_back(static_cast<char>(value >> 8));
  out.push_back(static_cast<char>(value & 0xFF));
}

// A CNAME link from an owner to a target.
struct CnameLink {
  // Canonical owner name.
  std::string owner;
  // Canonical target name.
  std::string target;
  // TTL of the CNAME record.
  uint32 ttl;
};

// An address record with its owner.
struct OwnedAddress {
  // Canonical owner name.
  std::string owner;
  // Record type (A or AAAA).
  uint16 type;
  // The address and TTL.
  DnsAddressRecord record;
};

}  // namespace

std::string CanonicalDnsName(std::string_view name) {
  if (!name.empty() && name.back() == '.') name.remove_suffix(1);
  std::string canonical(name);
  for (char& c : canonical)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return canonical;
}

std::optional<std::string> EncodeDnsQuery(uint16 id, std::string_view name,
                                          DnsRecordType type) {
  if (!name.empty() && name.back() == '.') name.remove_suffix(1);
  if (name.empty()) return std::nullopt;

  std::string packet;
  AppendUint16(packet, id);
  AppendUint16(packet, kRecursionDesiredFlag);
  AppendUint16(packet, 1);  // QDCOUNT
  AppendUint16(packet, 0);  // ANCOUNT
  AppendUint16(packet, 0);  // NSCOUNT
  AppendUint16(packet, 0);  // ARCOUNT

  size_t encoded_length = 1;  // Root label.
  while (true) {
    size_t dot = name.find('.');
    std::string_view label = name.substr(0, dot);
    if (label.empty() || label.size() > kMaxLabelLength) return std::nullopt;
    encoded_length += label.size() + 1;
    if (encoded_length > kMaxEncodedNameLength) return std::nullopt;
    packet.push_back(static_cast<char>(label.size()));
    packet.append(label);
    if (dot == std::string_view::npos) break;
    name.remove_prefix(dot + 1);
  }
  packet.push_back('\0');
  AppendUint16(packet, static_cast<uint16>(type));
  AppendUint16(packet, kClassInternet);
  return packet;
}

std::optional<DnsResponse> DecodeDnsResponse(std::string_view packet) {
  MessageReader reader(packet);
  if (packet.size() < kHeaderSize) return std::nullopt;
  uint16 flags = *reader.ReadUint16(2);
  if ((flags & kResponseFlag) == 0) return std::nullopt;
  uint16 question_count = *reader.ReadUint16(4);
  uint16 answer_count = *reader.ReadUint16(6);
  uint16 authority_count = *reader.ReadUint16(8);
  if (question_count > 1) return std::nullopt;

  DnsResponse response;
  response.id = *reader.ReadUint16(0);
  response.truncated = (flags & kTruncatedFlag) != 0;
  response.response_code =
      static_cast<DnsResponseCode>(flags & kResponseCodeMask);

  size_t offset = kHeaderSize;
  if (question_count == 1) {
    DnsQuestion question;
    if (!reader.ReadName(offset, question.name)) return std::nullopt;
    auto type = reader.ReadUint16(offset);
    auto query_class = reader.ReadUint16(offset + 2);
    if (!type || !query_class) return std::nullopt;
    question.type = *type;
    question.query_class = *query_class;
    offset += kQuestionFixedSize;
    response.question = std::move(question);
  }

  std::vector<CnameLink> cnames;
  std::vector<OwnedAddress> owned_addresses;
  for (uint16 i = 0; i < answer_count; i++) {
    auto record = ReadRecord(reader, offset);
    if (!record) return std::nullopt;
    if (record->record_class != kClassInternet) continue;
    if (record->type == static_cast<uint16>(DnsRecordType::A) ||
        record->type == static_cast<uint16>(DnsRecordType::Aaaa)) {
      bool is_v4 = record->type == static_cast<uint16>(DnsRecordType::A);
      size_t expected_size = is_v4 ? kARecordSize : kAaaaRecordSize;
      if (record->data_length != expected_size) return std::nullopt;
      std::string_view data =
          *reader.Slice(record->data_offset, record->data_length);
      auto address = ::perception::network::IpAddress::FromBytes(
          is_v4 ? ::perception::network::IpAddressFamily::V4
                : ::perception::network::IpAddressFamily::V6,
          std::span<const uint8>(reinterpret_cast<const uint8*>(data.data()),
                                 data.size()));
      owned_addresses.push_back(
          {record->owner, record->type, {address, record->ttl}});
    } else if (record->type == static_cast<uint16>(DnsRecordType::Cname)) {
      size_t name_offset = record->data_offset;
      std::string target;
      if (!ReadNameInRecordData(reader, *record, name_offset, target))
        return std::nullopt;
      cnames.push_back({record->owner, std::move(target), record->ttl});
    }
  }

  for (uint16 i = 0; i < authority_count; i++) {
    auto record = ReadRecord(reader, offset);
    if (!record) return std::nullopt;
    if (record->record_class != kClassInternet ||
        record->type != static_cast<uint16>(DnsRecordType::Soa))
      continue;
    size_t soa_offset = record->data_offset;
    std::string primary_server;
    std::string mailbox;
    if (!ReadNameInRecordData(reader, *record, soa_offset, primary_server) ||
        !ReadNameInRecordData(reader, *record, soa_offset, mailbox))
      return std::nullopt;
    if (soa_offset + kSoaFixedFieldsSize >
        record->data_offset + record->data_length)
      return std::nullopt;
    uint32 minimum = *reader.ReadUint32(soa_offset + kSoaFixedFieldsSize - 4);
    if (minimum & kTtlSignBit) minimum = 0;
    response.negative_ttl = std::min(record->ttl, minimum);
  }

  // The additional section is not needed and is not parsed.

  std::string name = response.question ? response.question->name : "";
  uint32 chain_ttl = ~0u;
  for (int link = 0; link < kMaxCnameChainLength; link++) {
    auto next = std::find_if(cnames.begin(), cnames.end(),
                             [&](const CnameLink& c) { return c.owner == name; });
    if (next == cnames.end()) break;
    chain_ttl = std::min(chain_ttl, next->ttl);
    name = next->target;
  }
  response.canonical_name = name;

  for (const OwnedAddress& owned : owned_addresses) {
    if (owned.owner != name) continue;
    if (response.question && owned.type != response.question->type) continue;
    DnsAddressRecord record = owned.record;
    record.ttl = std::min(record.ttl, chain_ttl);
    response.addresses.push_back(record);
  }
  return response;
}

bool IsResponseToQuery(const DnsResponse& response, uint16 id,
                       std::string_view name, DnsRecordType type) {
  return response.id == id && response.question &&
         response.question->query_class == kClassInternet &&
         response.question->type == static_cast<uint16>(type) &&
         response.question->name == CanonicalDnsName(name);
}
