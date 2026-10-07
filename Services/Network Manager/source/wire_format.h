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
#include <string>
#include <string_view>

#include "perception/network/ip_address.h"

// Reads big-endian fields from a byte buffer. Every read is bounds-checked;
// a read past the end returns zero and permanently clears ok().
class WireReader {
 public:
  explicit WireReader(std::string_view data) : data_(data) {}

  // Reads an 8-bit value.
  uint8 ReadU8() {
    if (!Require(1)) return 0;
    return static_cast<uint8>(data_[offset_++]);
  }

  // Reads a big-endian 16-bit value.
  uint16 ReadU16() {
    if (!Require(2)) return 0;
    uint16 value = (Byte(0) << 8) | Byte(1);
    offset_ += 2;
    return value;
  }

  // Reads a big-endian 24-bit value.
  uint32 ReadU24() {
    if (!Require(3)) return 0;
    uint32 value = (Byte(0) << 16) | (Byte(1) << 8) | Byte(2);
    offset_ += 3;
    return value;
  }

  // Reads a big-endian 32-bit value.
  uint32 ReadU32() {
    if (!Require(4)) return 0;
    uint32 value = (Byte(0) << 24) | (Byte(1) << 16) | (Byte(2) << 8) | Byte(3);
    offset_ += 4;
    return value;
  }

  // Reads `length` raw bytes.
  std::string_view ReadBytes(size_t length) {
    if (!Require(length)) return {};
    std::string_view bytes = data_.substr(offset_, length);
    offset_ += length;
    return bytes;
  }

  // Reads a 16-byte IPv6 address.
  ::perception::network::IpAddress ReadIpv6Address() {
    std::array<uint8, 16> bytes{};
    std::string_view raw = ReadBytes(bytes.size());
    for (size_t i = 0; i < raw.size(); i++)
      bytes[i] = static_cast<uint8>(raw[i]);
    return ::perception::network::IpAddress::V6(bytes);
  }

  // Skips `length` bytes.
  void Skip(size_t length) {
    if (Require(length)) offset_ += length;
  }

  // Returns the bytes not yet read.
  std::string_view Rest() const { return ok_ ? data_.substr(offset_) : ""; }

  // Returns the number of bytes not yet read.
  size_t Remaining() const { return ok_ ? data_.size() - offset_ : 0; }

  // Returns the read position.
  size_t Offset() const { return offset_; }

  // Returns false if any read overran the buffer.
  bool ok() const { return ok_; }

 private:
  // Checks that `length` more bytes are available, clearing ok() if not.
  bool Require(size_t length) {
    if (ok_ && data_.size() - offset_ >= length) return true;
    ok_ = false;
    return false;
  }

  // Returns the byte at `index` past the read position.
  uint32 Byte(size_t index) const {
    return static_cast<uint8>(data_[offset_ + index]);
  }

  // The buffer being read.
  std::string_view data_;
  // Read position.
  size_t offset_ = 0;
  // False once a read has overrun the buffer.
  bool ok_ = true;
};

// Appends big-endian fields to a byte buffer.
class WireWriter {
 public:
  explicit WireWriter(std::string& out) : out_(out) {}

  // Appends an 8-bit value.
  void WriteU8(uint8 value) { out_.push_back(static_cast<char>(value)); }

  // Appends a big-endian 16-bit value.
  void WriteU16(uint16 value) {
    WriteU8(value >> 8);
    WriteU8(value & 0xFF);
  }

  // Appends a big-endian 24-bit value.
  void WriteU24(uint32 value) {
    WriteU8((value >> 16) & 0xFF);
    WriteU16(value & 0xFFFF);
  }

  // Appends a big-endian 32-bit value.
  void WriteU32(uint32 value) {
    WriteU16(value >> 16);
    WriteU16(value & 0xFFFF);
  }

  // Appends raw bytes.
  void WriteBytes(std::string_view bytes) { out_.append(bytes); }

  // Appends `count` zero bytes.
  void WriteZeros(size_t count) { out_.append(count, '\0'); }

  // Appends the 16 bytes of an IPv6 address.
  void WriteIpv6Address(const ::perception::network::IpAddress& address) {
    for (uint8 byte : address.bytes()) WriteU8(byte);
  }

  // Overwrites a big-endian 16-bit value at `offset`.
  void PatchU16(size_t offset, uint16 value) {
    out_[offset] = static_cast<char>(value >> 8);
    out_[offset + 1] = static_cast<char>(value & 0xFF);
  }

  // Returns the current buffer size.
  size_t Size() const { return out_.size(); }

 private:
  // The buffer being appended to.
  std::string& out_;
};
