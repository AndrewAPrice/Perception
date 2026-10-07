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

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace perception {
namespace http {

namespace {

// Default maximum size in bytes of the HPACK dynamic table per RFC 7541.
constexpr size_t kDefaultHpackDynamicTableSize = 4096;

}  // namespace

// Represents a single HTTP header name-value pair.
struct HeaderField {
  std::string name;
  std::string value;

  bool operator==(const HeaderField& other) const {
    return name == other.name && value == other.value;
  }
};

// Encodes an integer using HPACK prefix integer representation (RFC 7541 5.1).
void EncodeHpackInteger(uint64_t value, uint8_t prefix_bits_count,
                        uint8_t prefix_flags, std::vector<uint8_t>& output);

// Decodes an integer using HPACK prefix integer representation (RFC 7541 5.1).
bool DecodeHpackInteger(std::span<const uint8_t> input, size_t& offset,
                        uint8_t prefix_bits_count, uint64_t& value);

// Encodes a string into canonical HPACK Huffman codes (RFC 7541 Appendix B).
void EncodeHpackHuffman(std::string_view input, std::vector<uint8_t>& output);

// Decodes an HPACK Huffman-encoded byte span (RFC 7541 Appendix B).
bool DecodeHpackHuffman(std::span<const uint8_t> input, std::string& output);

// Encodes an HPACK string literal with optional Huffman coding (RFC 7541 5.2).
void EncodeHpackStringLiteral(std::string_view value, bool use_huffman,
                              std::vector<uint8_t>& output);

// Decodes an HPACK string literal (raw or Huffman-coded) (RFC 7541 5.2).
bool DecodeHpackStringLiteral(std::span<const uint8_t> input, size_t& offset,
                              std::string& output);

// Returns the static table entry at the 1-based index [1..61], or nullptr.
const HeaderField* GetHpackStaticTableEntry(size_t index);

// Manages the HPACK dynamic header table and its byte-size eviction rules.
class HpackDynamicTable {
 public:
  explicit HpackDynamicTable(
      size_t max_size = kDefaultHpackDynamicTableSize);

  // Updates the maximum dynamic table capacity and evicts excess entries.
  void SetMaxSize(size_t max_size);

  // Inserts a new header field at the front of the dynamic table.
  void Insert(std::string_view name, std::string_view value);

  // Looks up a 1-based combined table index (>= 62). Returns nullptr if invalid.
  const HeaderField* GetByCombinedIndex(size_t combined_index) const;

  // Searches for a matching entry in the dynamic table and returns its combined
  // index (or 0 if not found). Sets exact_match to true if both name and value
  // match.
  size_t Find(std::string_view name, std::string_view value,
              bool& exact_match) const;

  // Returns the current byte size of all entries in the dynamic table.
  size_t CurrentSize() const { return current_size_; }

  // Returns the configured maximum byte size of the dynamic table.
  size_t MaxSize() const { return max_size_; }

  // Returns the number of entries currently stored in the dynamic table.
  size_t EntryCount() const { return entries_.size(); }

 private:
  void EvictToSize(size_t target_size);

  std::deque<HeaderField> entries_;
  size_t current_size_ = 0;
  size_t max_size_ = kDefaultHpackDynamicTableSize;
};

// Encodes HTTP/2 header lists into HPACK header block fragments (RFC 7541).
class HpackEncoder {
 public:
  explicit HpackEncoder(
      size_t max_table_size = kDefaultHpackDynamicTableSize,
      bool use_huffman = false);

  // Updates the peer's maximum allowed dynamic table size from SETTINGS.
  void SetMaxTableSize(size_t max_table_size);

  // Configures whether string literals should be Huffman-encoded.
  void SetUseHuffman(bool use_huffman) { use_huffman_ = use_huffman; }

  // Encodes a list of headers and appends the resulting bytes to output.
  void Encode(std::span<const HeaderField> headers,
              std::vector<uint8_t>& output);

  // Encodes a single header field and appends the resulting bytes to output.
  void EncodeField(const HeaderField& field, bool allow_indexing,
                   std::vector<uint8_t>& output);

  // Returns a const reference to the encoder's dynamic table.
  const HpackDynamicTable& DynamicTable() const { return dynamic_table_; }

 private:
  HpackDynamicTable dynamic_table_;
  bool use_huffman_ = false;
  bool pending_table_size_update_ = false;
  size_t smallest_pending_table_size_ = kDefaultHpackDynamicTableSize;
  size_t latest_pending_table_size_ = kDefaultHpackDynamicTableSize;
};

// Decodes HPACK header block fragments into HTTP/2 header lists (RFC 7541).
class HpackDecoder {
 public:
  explicit HpackDecoder(
      size_t max_allowed_table_size = kDefaultHpackDynamicTableSize);

  // Sets the upper bound allowed for dynamic table size updates.
  void SetMaxAllowedTableSize(size_t max_allowed_table_size);

  // Decodes a complete HPACK header block into the headers vector.
  bool Decode(std::span<const uint8_t> block,
              std::vector<HeaderField>& headers);

  // Returns a const reference to the decoder's dynamic table.
  const HpackDynamicTable& DynamicTable() const { return dynamic_table_; }

 private:
  const HeaderField* LookupIndex(size_t index) const;

  HpackDynamicTable dynamic_table_;
  size_t max_allowed_table_size_ = kDefaultHpackDynamicTableSize;
};

}  // namespace http
}  // namespace perception
