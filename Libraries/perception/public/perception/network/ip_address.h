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

#include <array>
#include <compare>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "perception/serialization/serializable.h"

namespace perception {
namespace serialization {
class Serializer;
}

namespace network {

// Address family of an IpAddress.
enum class IpAddressFamily : uint8 { Unspecified = 0, V4 = 4, V6 = 6 };

// An IPv4 or IPv6 address in network byte order. IPv4 uses bytes()[0..3] and
// the remaining bytes are zero. IPv4-mapped IPv6 addresses are normalized to
// V4 by Parse(), so inside the system IPv4 has exactly one representation.
class IpAddress : public serialization::Serializable {
 public:
  // Number of bytes in an IPv4 address.
  static constexpr size_t kV4Length = 4;

  // Number of bytes in an IPv6 address.
  static constexpr size_t kV6Length = 16;

  // Constructs an address with the Unspecified family (all bytes zero).
  IpAddress() = default;

  // Builds an IPv4 address from its four octets.
  static IpAddress V4(uint8 a, uint8 b, uint8 c, uint8 d);

  // Builds an IPv4 address from 4 bytes in network order.
  static IpAddress V4(const std::array<uint8, kV4Length>& address_bytes);

  // Builds an IPv6 address from 16 bytes in network order. This does not
  // normalize IPv4-mapped addresses.
  static IpAddress V6(const std::array<uint8, kV6Length>& address_bytes);

  // Builds an address of `family` from the first 4 (V4) or 16 (V6) bytes of
  // `data`, in network order. Returns an Unspecified-family address if the
  // family is Unspecified or `data` is too short.
  static IpAddress FromBytes(IpAddressFamily family,
                             std::span<const uint8> data);

  // Returns the IPv4 limited broadcast address 255.255.255.255.
  static IpAddress V4Broadcast();

  // Returns the IPv4 unspecified address 0.0.0.0.
  static IpAddress V4Any();

  // Returns the IPv6 unspecified address ::.
  static IpAddress V6Any();

  // Parses dotted-quad, RFC 4291 text (including an embedded dotted-quad
  // tail), or a bracketed "[v6]" literal. IPv4-mapped IPv6 input is
  // normalized to a V4 address. Zone identifiers ("%eth0") are rejected.
  static std::optional<IpAddress> Parse(std::string_view text);

  // Returns the address family.
  IpAddressFamily family() const { return family_; }

  // Returns all 16 storage bytes in network order. Only the first Length()
  // bytes are meaningful.
  const std::array<uint8, kV6Length>& bytes() const { return bytes_; }

  // Returns the number of meaningful bytes: 4 (V4), 16 (V6) or 0.
  size_t Length() const;

  // Returns true if the family is V4.
  bool IsV4() const { return family_ == IpAddressFamily::V4; }

  // Returns true if the family is V6.
  bool IsV6() const { return family_ == IpAddressFamily::V6; }

  // Copies the Length() meaningful bytes into the start of `destination`.
  // Does nothing if `destination` is shorter than Length().
  void CopyTo(std::span<uint8> destination) const;

  // Formats dotted-quad for V4 and RFC 5952 text for V6 (lowercase, longest
  // run of two or more zero groups compressed, first run on ties). Returns ""
  // for the Unspecified family.
  std::string ToString() const override;

  // Returns true for the Unspecified family, 0.0.0.0, or ::.
  bool IsUnspecified() const;

  // Returns true for 127.0.0.0/8 or ::1.
  bool IsLoopback() const;

  // Returns true for 169.254.0.0/16 or fe80::/10.
  bool IsLinkLocal() const;

  // Returns true for 224.0.0.0/4 or ff00::/8.
  bool IsMulticast() const;

  // Returns true for 255.255.255.255.
  bool IsBroadcast() const;

  // Returns true if this address and `prefix` share a family and their first
  // `prefix_length` bits are equal. Lengths beyond the address size are
  // clamped.
  bool IsInPrefix(const IpAddress& prefix, uint8 prefix_length) const;

  // Returns the number of leading bits shared with `other`, or 0 if the
  // families differ.
  uint8 CommonPrefixLength(const IpAddress& other) const;

  // Orders by family, then by bytes.
  bool operator==(const IpAddress& other) const;
  std::strong_ordering operator<=>(const IpAddress& other) const;

  void Serialize(serialization::Serializer& serializer) override;

 private:
  // Family of the stored address.
  IpAddressFamily family_ = IpAddressFamily::Unspecified;

  // Address bytes in network order; unused bytes are zero.
  std::array<uint8, kV6Length> bytes_{};
};

}  // namespace network
}  // namespace perception
