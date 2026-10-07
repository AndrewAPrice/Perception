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

#include <algorithm>
#include <vector>

#include "perception/serialization/serializer.h"

namespace perception {
namespace network {
namespace {

// Number of 16-bit groups in an IPv6 address.
constexpr int kV6Groups = 8;

// Maximum number of hex digits in an IPv6 group.
constexpr size_t kMaxHexDigitsPerGroup = 4;

// Maximum number of decimal digits in a dotted-quad octet.
constexpr size_t kMaxDecimalDigitsPerOctet = 3;

// Offset of the 0xFFFF marker in an IPv4-mapped IPv6 address (::ffff:0:0/96).
constexpr size_t kV4MappedMarkerOffset = 10;

// Offset of the embedded IPv4 address in an IPv4-mapped IPv6 address.
constexpr size_t kV4MappedAddressOffset = 12;

// Number of bits in a byte.
constexpr int kBitsPerByte = 8;

// Lowercase hexadecimal digits for formatting.
constexpr char kHexDigits[] = "0123456789abcdef";

// Returns the value of a hex digit, or -1 if `c` is not one.
int HexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Parses a dotted-quad IPv4 address into `out`.
bool ParseDottedQuad(std::string_view text, std::array<uint8, 4>& out) {
  int octet = 0;
  size_t digits = 0;
  int value = 0;
  for (char c : text) {
    if (c == '.') {
      if (digits == 0 || octet >= 3) return false;
      out[octet++] = static_cast<uint8>(value);
      digits = 0;
      value = 0;
    } else if (c >= '0' && c <= '9') {
      if (++digits > kMaxDecimalDigitsPerOctet) return false;
      value = value * 10 + (c - '0');
      if (value > 255) return false;
    } else {
      return false;
    }
  }
  if (digits == 0 || octet != 3) return false;
  out[octet] = static_cast<uint8>(value);
  return true;
}

// Parses colon-separated IPv6 groups (no "::") and appends their bytes to
// `out`. If `allow_v4_tail` is set, the last group may be a dotted quad.
bool ParseV6Groups(std::string_view text, bool allow_v4_tail,
                   std::vector<uint8>& out) {
  if (text.empty()) return true;
  while (true) {
    size_t colon = text.find(':');
    std::string_view group = text.substr(0, colon);
    bool is_last = colon == std::string_view::npos;
    if (group.empty()) return false;

    if (group.find('.') != std::string_view::npos) {
      if (!is_last || !allow_v4_tail) return false;
      std::array<uint8, 4> v4;
      if (!ParseDottedQuad(group, v4)) return false;
      out.insert(out.end(), v4.begin(), v4.end());
      return true;
    }

    if (group.size() > kMaxHexDigitsPerGroup) return false;
    int value = 0;
    for (char c : group) {
      int digit = HexValue(c);
      if (digit < 0) return false;
      value = (value << 4) | digit;
    }
    out.push_back(static_cast<uint8>(value >> 8));
    out.push_back(static_cast<uint8>(value & 0xFF));

    if (is_last) return true;
    text = text.substr(colon + 1);
  }
}

// Parses RFC 4291 IPv6 text into 16 bytes.
std::optional<std::array<uint8, 16>> ParseV6Bytes(std::string_view text) {
  std::vector<uint8> head;
  std::vector<uint8> tail;
  size_t double_colon = text.find("::");
  if (double_colon == std::string_view::npos) {
    if (!ParseV6Groups(text, /*allow_v4_tail=*/true, head)) return std::nullopt;
    if (head.size() != IpAddress::kV6Length) return std::nullopt;
  } else {
    std::string_view head_text = text.substr(0, double_colon);
    std::string_view tail_text = text.substr(double_colon + 2);
    if (tail_text.find("::") != std::string_view::npos) return std::nullopt;
    if (!ParseV6Groups(head_text, /*allow_v4_tail=*/false, head) ||
        !ParseV6Groups(tail_text, /*allow_v4_tail=*/true, tail))
      return std::nullopt;
    // "::" must stand for at least one zero group.
    if (head.size() + tail.size() > IpAddress::kV6Length - 2)
      return std::nullopt;
  }

  std::array<uint8, 16> bytes{};
  std::copy(head.begin(), head.end(), bytes.begin());
  std::copy(tail.begin(), tail.end(), bytes.end() - tail.size());
  return bytes;
}

// Returns true if `bytes` is an IPv4-mapped IPv6 address (::ffff:0:0/96).
bool IsV4MappedBytes(const std::array<uint8, 16>& bytes) {
  for (size_t i = 0; i < kV4MappedMarkerOffset; i++) {
    if (bytes[i] != 0) return false;
  }
  return bytes[kV4MappedMarkerOffset] == 0xFF &&
         bytes[kV4MappedMarkerOffset + 1] == 0xFF;
}

// Appends the dotted-quad form of 4 bytes starting at `offset` to `out`.
void AppendDottedQuad(const std::array<uint8, 16>& bytes, size_t offset,
                      std::string& out) {
  for (size_t i = 0; i < IpAddress::kV4Length; i++) {
    if (i > 0) out += '.';
    out += std::to_string(bytes[offset + i]);
  }
}

// Appends a 16-bit group in lowercase hex without leading zeros to `out`.
void AppendHexGroup(uint16 group, std::string& out) {
  bool started = false;
  for (int shift = 12; shift >= 0; shift -= 4) {
    int digit = (group >> shift) & 0xF;
    if (digit == 0 && !started && shift != 0) continue;
    started = true;
    out += kHexDigits[digit];
  }
}

}  // namespace

IpAddress IpAddress::V4(uint8 a, uint8 b, uint8 c, uint8 d) {
  return V4(std::array<uint8, kV4Length>{a, b, c, d});
}

IpAddress IpAddress::V4(const std::array<uint8, kV4Length>& address_bytes) {
  IpAddress address;
  address.family_ = IpAddressFamily::V4;
  std::copy(address_bytes.begin(), address_bytes.end(), address.bytes_.begin());
  return address;
}

IpAddress IpAddress::V6(const std::array<uint8, kV6Length>& address_bytes) {
  IpAddress address;
  address.family_ = IpAddressFamily::V6;
  address.bytes_ = address_bytes;
  return address;
}

IpAddress IpAddress::FromBytes(IpAddressFamily family,
                               std::span<const uint8> data) {
  IpAddress address;
  address.family_ = family;
  size_t length = address.Length();
  if (length == 0 || data.size() < length) return IpAddress();
  std::copy(data.begin(), data.begin() + length, address.bytes_.begin());
  return address;
}

IpAddress IpAddress::V4Broadcast() { return V4(255, 255, 255, 255); }

IpAddress IpAddress::V4Any() { return V4(0, 0, 0, 0); }

IpAddress IpAddress::V6Any() { return V6({}); }

std::optional<IpAddress> IpAddress::Parse(std::string_view text) {
  bool bracketed = false;
  if (!text.empty() && text.front() == '[') {
    if (text.size() < 2 || text.back() != ']') return std::nullopt;
    text = text.substr(1, text.size() - 2);
    bracketed = true;
  }

  if (text.find(':') == std::string_view::npos) {
    if (bracketed) return std::nullopt;
    std::array<uint8, kV4Length> v4;
    if (!ParseDottedQuad(text, v4)) return std::nullopt;
    return V4(v4);
  }

  std::optional<std::array<uint8, kV6Length>> v6 = ParseV6Bytes(text);
  if (!v6) return std::nullopt;
  if (IsV4MappedBytes(*v6)) {
    return FromBytes(IpAddressFamily::V4,
                     std::span<const uint8>(*v6).subspan(kV4MappedAddressOffset));
  }
  return V6(*v6);
}

size_t IpAddress::Length() const {
  switch (family_) {
    case IpAddressFamily::V4:
      return kV4Length;
    case IpAddressFamily::V6:
      return kV6Length;
    default:
      return 0;
  }
}

void IpAddress::CopyTo(std::span<uint8> destination) const {
  size_t length = Length();
  if (destination.size() < length) return;
  std::copy(bytes_.begin(), bytes_.begin() + length, destination.begin());
}

std::string IpAddress::ToString() const {
  std::string out;
  if (family_ == IpAddressFamily::V4) {
    AppendDottedQuad(bytes_, 0, out);
    return out;
  }
  if (family_ != IpAddressFamily::V6) return out;

  if (IsV4MappedBytes(bytes_)) {
    out = "::ffff:";
    AppendDottedQuad(bytes_, kV4MappedAddressOffset, out);
    return out;
  }

  std::array<uint16, kV6Groups> groups;
  for (int i = 0; i < kV6Groups; i++)
    groups[i] = static_cast<uint16>((bytes_[i * 2] << 8) | bytes_[i * 2 + 1]);

  // Longest run of zero groups; only runs of two or more are compressed.
  int best_start = -1;
  int best_length = 1;
  for (int i = 0; i < kV6Groups;) {
    if (groups[i] != 0) {
      i++;
      continue;
    }
    int start = i;
    while (i < kV6Groups && groups[i] == 0) i++;
    if (i - start > best_length) {
      best_start = start;
      best_length = i - start;
    }
  }

  for (int i = 0; i < kV6Groups; i++) {
    if (i == best_start) {
      out += "::";
      i += best_length - 1;
      continue;
    }
    if (!out.empty() && out.back() != ':') out += ':';
    AppendHexGroup(groups[i], out);
  }
  return out;
}

bool IpAddress::IsUnspecified() const {
  return std::all_of(bytes_.begin(), bytes_.end(),
                     [](uint8 b) { return b == 0; });
}

bool IpAddress::IsLoopback() const {
  if (IsV4()) return bytes_[0] == 127;
  if (!IsV6()) return false;
  for (size_t i = 0; i < kV6Length - 1; i++) {
    if (bytes_[i] != 0) return false;
  }
  return bytes_[kV6Length - 1] == 1;
}

bool IpAddress::IsLinkLocal() const {
  if (IsV4()) return bytes_[0] == 169 && bytes_[1] == 254;
  if (IsV6()) return bytes_[0] == 0xFE && (bytes_[1] & 0xC0) == 0x80;
  return false;
}

bool IpAddress::IsMulticast() const {
  if (IsV4()) return (bytes_[0] & 0xF0) == 0xE0;
  if (IsV6()) return bytes_[0] == 0xFF;
  return false;
}

bool IpAddress::IsBroadcast() const { return *this == V4Broadcast(); }

bool IpAddress::IsInPrefix(const IpAddress& prefix,
                           uint8 prefix_length) const {
  if (family_ != prefix.family_ || Length() == 0) return false;
  return CommonPrefixLength(prefix) >=
         std::min<size_t>(prefix_length, Length() * kBitsPerByte);
}

uint8 IpAddress::CommonPrefixLength(const IpAddress& other) const {
  if (family_ != other.family_) return 0;
  uint8 bits = 0;
  for (size_t i = 0; i < Length(); i++) {
    uint8 difference = bytes_[i] ^ other.bytes_[i];
    if (difference == 0) {
      bits += kBitsPerByte;
      continue;
    }
    for (int bit = kBitsPerByte - 1; bit >= 0; bit--) {
      if (difference & (1 << bit)) break;
      bits++;
    }
    break;
  }
  return bits;
}

bool IpAddress::operator==(const IpAddress& other) const {
  return family_ == other.family_ && bytes_ == other.bytes_;
}

std::strong_ordering IpAddress::operator<=>(const IpAddress& other) const {
  if (auto order = family_ <=> other.family_; order != 0) return order;
  return bytes_ <=> other.bytes_;
}

void IpAddress::Serialize(serialization::Serializer& serializer) {
  uint64 high = 0;
  uint64 low = 0;
  for (int i = 0; i < 8; i++) {
    high = (high << 8) | bytes_[i];
    low = (low << 8) | bytes_[i + 8];
  }
  serializer.Enum("family", family_);
  serializer.Integer("high", high);
  serializer.Integer("low", low);
  if (!serializer.IsDeserializing()) return;
  for (int i = 7; i >= 0; i--) {
    bytes_[i] = static_cast<uint8>(high & 0xFF);
    bytes_[i + 8] = static_cast<uint8>(low & 0xFF);
    high >>= 8;
    low >>= 8;
  }
}

}  // namespace network
}  // namespace perception
