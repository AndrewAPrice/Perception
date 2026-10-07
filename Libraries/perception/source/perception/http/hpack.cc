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

#include "perception/http/hpack.h"

#include <algorithm>
#include <array>

namespace perception {
namespace http {

namespace {

// Number of entries in the HPACK static table (RFC 7541 Appendix A).
constexpr size_t kStaticTableSize = 61;

// Per-entry byte overhead in the HPACK dynamic table (RFC 7541 Section 4.1).
constexpr size_t kDynamicEntryOverheadBytes = 32;

// Total number of symbols in the HPACK Huffman code (256 octets + EOS).
constexpr size_t kHuffmanSymbolCount = 257;

// Symbol index representing End-Of-String (EOS) in HPACK Huffman coding.
constexpr uint16_t kHuffmanEosSymbol = 256;

// Maximum shift allowed when decoding a variable-length HPACK integer.
constexpr uint8_t kMaxIntegerShift = 56;

// Maximum number of trailing padding bits in a valid Huffman string.
constexpr uint8_t kMaxHuffmanPaddingBits = 7;

// Prefix byte bitmask for an indexed header field representation.
constexpr uint8_t kIndexedHeaderMask = 0x80;

// Prefix byte bitmask for a literal header field with incremental indexing.
constexpr uint8_t kLiteralIncrementalMask = 0xC0;

// Prefix byte value for a literal header field with incremental indexing.
constexpr uint8_t kLiteralIncrementalFlag = 0x40;

// Prefix byte bitmask for a dynamic table size update.
constexpr uint8_t kTableSizeUpdateMask = 0xE0;

// Prefix byte value for a dynamic table size update.
constexpr uint8_t kTableSizeUpdateFlag = 0x20;

// Prefix byte bitmask for literal without indexing or never-indexed fields.
constexpr uint8_t kLiteralUnindexedMask = 0xF0;

// Prefix byte value for a literal header field never indexed.
constexpr uint8_t kLiteralNeverIndexedFlag = 0x10;

// Prefix byte flag indicating a Huffman-encoded string literal.
constexpr uint8_t kStringHuffmanFlag = 0x80;

// Prefix bit count for indexed header field representation.
constexpr uint8_t kIndexedPrefixBits = 7;

// Prefix bit count for literal header field with incremental indexing.
constexpr uint8_t kLiteralIncrementalPrefixBits = 6;

// Prefix bit count for dynamic table size update.
constexpr uint8_t kTableSizeUpdatePrefixBits = 5;

// Prefix bit count for literal header field without indexing / never indexed.
constexpr uint8_t kLiteralUnindexedPrefixBits = 4;

// Prefix bit count for string literal length.
constexpr uint8_t kStringLengthPrefixBits = 7;

// Bit lengths for each of the 257 symbols (0..255 + EOS) in RFC 7541 Appendix B.
constexpr std::array<uint8_t, kHuffmanSymbolCount> kHuffmanBitLengths = {
    13, 23, 28, 28, 28, 28, 28, 28, 28, 24, 30, 28, 28, 30, 28, 28,
    28, 28, 28, 28, 28, 28, 30, 28, 28, 28, 28, 28, 28, 28, 28, 28,
    6,  10, 10, 12, 13, 6,  8,  11, 10, 10, 8,  11, 8,  6,  6,  6,
    5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  7,  8,  15, 6,  12, 10,
    13, 6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
    7,  7,  7,  7,  7,  7,  7,  7,  8,  7,  8,  13, 19, 13, 14, 6,
    15, 5,  6,  5,  6,  5,  6,  6,  6,  5,  7,  7,  6,  6,  6,  5,
    6,  7,  6,  5,  5,  6,  7,  7,  7,  7,  7,  15, 11, 14, 13, 28,
    20, 22, 20, 20, 22, 22, 22, 23, 22, 23, 23, 23, 23, 23, 24, 23,
    24, 24, 22, 23, 24, 23, 23, 23, 23, 21, 22, 23, 22, 23, 23, 24,
    22, 21, 20, 22, 22, 23, 23, 21, 23, 22, 22, 24, 21, 22, 23, 23,
    21, 21, 22, 21, 23, 22, 23, 23, 20, 22, 22, 22, 23, 22, 22, 23,
    26, 26, 20, 19, 22, 23, 22, 25, 26, 26, 26, 27, 27, 26, 24, 25,
    19, 21, 26, 27, 27, 26, 27, 24, 21, 21, 26, 26, 28, 27, 27, 27,
    20, 24, 20, 21, 22, 21, 21, 23, 22, 22, 25, 25, 24, 24, 26, 23,
    26, 27, 26, 26, 27, 27, 27, 27, 27, 28, 27, 27, 27, 27, 27, 26,
    30};

struct HuffmanCodeEntry {
  uint32_t code = 0;
  uint8_t bit_length = 0;
};

constexpr std::array<HuffmanCodeEntry, kHuffmanSymbolCount>
BuildCanonicalHuffmanTable() {
  std::array<HuffmanCodeEntry, kHuffmanSymbolCount> table{};
  uint32_t current_code = 0;
  uint8_t prev_len = 0;
  for (uint8_t len = 5; len <= 30; ++len) {
    for (size_t sym = 0; sym < kHuffmanSymbolCount; ++sym) {
      if (kHuffmanBitLengths[sym] == len) {
        if (prev_len != 0)
          current_code <<= (len - prev_len);
        table[sym].code = current_code;
        table[sym].bit_length = len;
        ++current_code;
        prev_len = len;
      }
    }
  }
  return table;
}

// Canonical HPACK Huffman code table (RFC 7541 Appendix B).
constexpr auto kHuffmanTable = BuildCanonicalHuffmanTable();

struct HuffmanTrieNode {
  int16_t children[2] = {-1, -1};
  int16_t symbol = -1;
};

const std::vector<HuffmanTrieNode>& GetHuffmanDecodeTrie() {
  static const std::vector<HuffmanTrieNode> trie = []() {
    std::vector<HuffmanTrieNode> nodes(1);
    for (size_t sym = 0; sym < kHuffmanSymbolCount; ++sym) {
      uint32_t code = kHuffmanTable[sym].code;
      uint8_t len = kHuffmanTable[sym].bit_length;
      int16_t curr = 0;
      for (int bit_pos = len - 1; bit_pos >= 0; --bit_pos) {
        uint8_t bit = (code >> bit_pos) & 1u;
        if (nodes[curr].children[bit] < 0) {
          nodes[curr].children[bit] = static_cast<int16_t>(nodes.size());
          nodes.emplace_back();
        }
        curr = nodes[curr].children[bit];
      }
      nodes[curr].symbol = static_cast<int16_t>(sym);
    }
    return nodes;
  }();
  return trie;
}

const std::array<HeaderField, kStaticTableSize>& GetStaticTable() {
  static const std::array<HeaderField, kStaticTableSize> table = {{
      {":authority", ""},
      {":method", "GET"},
      {":method", "POST"},
      {":path", "/"},
      {":path", "/index.html"},
      {":scheme", "http"},
      {":scheme", "https"},
      {":status", "200"},
      {":status", "204"},
      {":status", "206"},
      {":status", "304"},
      {":status", "400"},
      {":status", "404"},
      {":status", "500"},
      {"accept-charset", ""},
      {"accept-encoding", "gzip, deflate"},
      {"accept-language", ""},
      {"accept-ranges", ""},
      {"accept", ""},
      {"access-control-allow-origin", ""},
      {"age", ""},
      {"allow", ""},
      {"authorization", ""},
      {"cache-control", ""},
      {"content-disposition", ""},
      {"content-encoding", ""},
      {"content-language", ""},
      {"content-length", ""},
      {"content-location", ""},
      {"content-range", ""},
      {"content-type", ""},
      {"cookie", ""},
      {"date", ""},
      {"etag", ""},
      {"expect", ""},
      {"expires", ""},
      {"from", ""},
      {"host", ""},
      {"if-match", ""},
      {"if-modified-since", ""},
      {"if-none-match", ""},
      {"if-range", ""},
      {"if-unmodified-since", ""},
      {"last-modified", ""},
      {"link", ""},
      {"location", ""},
      {"max-forwards", ""},
      {"proxy-authenticate", ""},
      {"proxy-authorization", ""},
      {"range", ""},
      {"referer", ""},
      {"refresh", ""},
      {"retry-after", ""},
      {"server", ""},
      {"set-cookie", ""},
      {"strict-transport-security", ""},
      {"transfer-encoding", ""},
      {"user-agent", ""},
      {"vary", ""},
      {"via", ""},
      {"www-authenticate", ""},
  }};
  return table;
}

size_t FindInStaticTable(std::string_view name, std::string_view value,
                         bool& exact_match) {
  exact_match = false;
  size_t name_match_index = 0;
  const auto& table = GetStaticTable();
  for (size_t i = 0; i < table.size(); ++i) {
    if (table[i].name == name) {
      if (table[i].value == value) {
        exact_match = true;
        return i + 1;
      }
      if (name_match_index == 0)
        name_match_index = i + 1;
    }
  }
  return name_match_index;
}

bool IsSensitiveHeader(std::string_view name) {
  return name == "authorization" || name == "proxy-authorization" ||
         name == "cookie" || name == "set-cookie";
}

}  // namespace

void EncodeHpackInteger(uint64_t value, uint8_t prefix_bits_count,
                        uint8_t prefix_flags, std::vector<uint8_t>& output) {
  uint64_t max_prefix = (uint64_t{1} << prefix_bits_count) - 1;
  if (value < max_prefix) {
    output.push_back(static_cast<uint8_t>(prefix_flags | value));
    return;
  }

  output.push_back(static_cast<uint8_t>(prefix_flags | max_prefix));
  value -= max_prefix;
  while (value >= 128) {
    output.push_back(static_cast<uint8_t>((value & 0x7Fu) | 0x80u));
    value >>= 7;
  }
  output.push_back(static_cast<uint8_t>(value));
}

bool DecodeHpackInteger(std::span<const uint8_t> input, size_t& offset,
                        uint8_t prefix_bits_count, uint64_t& value) {
  if (offset >= input.size())
    return false;

  uint64_t max_prefix = (uint64_t{1} << prefix_bits_count) - 1;
  value = input[offset++] & max_prefix;
  if (value < max_prefix)
    return true;

  uint8_t shift = 0;
  while (offset < input.size()) {
    if (shift > kMaxIntegerShift)
      return false;
    uint8_t byte = input[offset++];
    value += static_cast<uint64_t>(byte & 0x7Fu) << shift;
    if ((byte & 0x80u) == 0)
      return true;
    shift += 7;
  }
  return false;
}

void EncodeHpackHuffman(std::string_view input, std::vector<uint8_t>& output) {
  uint64_t bit_buffer = 0;
  uint8_t bits_in_buffer = 0;

  for (unsigned char ch : input) {
    const auto& entry = kHuffmanTable[ch];
    bit_buffer = (bit_buffer << entry.bit_length) | entry.code;
    bits_in_buffer += entry.bit_length;
    while (bits_in_buffer >= 8) {
      bits_in_buffer -= 8;
      output.push_back(
          static_cast<uint8_t>((bit_buffer >> bits_in_buffer) & 0xFFu));
    }
  }

  if (bits_in_buffer > 0) {
    uint8_t pad_bits = 8 - bits_in_buffer;
    uint8_t last_byte = static_cast<uint8_t>(
        (bit_buffer << pad_bits) | ((1u << pad_bits) - 1u));
    output.push_back(last_byte);
  }
}

bool DecodeHpackHuffman(std::span<const uint8_t> input, std::string& output) {
  output.clear();
  const auto& trie = GetHuffmanDecodeTrie();
  int16_t node = 0;
  uint8_t current_bits = 0;
  bool padding_all_ones = true;

  for (uint8_t byte : input) {
    for (int bit_pos = 7; bit_pos >= 0; --bit_pos) {
      uint8_t bit = (byte >> bit_pos) & 1u;
      if (bit == 0)
        padding_all_ones = false;
      ++current_bits;

      node = trie[node].children[bit];
      if (node < 0)
        return false;

      if (trie[node].symbol >= 0) {
        if (static_cast<uint16_t>(trie[node].symbol) == kHuffmanEosSymbol)
          return false;
        output.push_back(static_cast<char>(trie[node].symbol));
        node = 0;
        current_bits = 0;
        padding_all_ones = true;
      }
    }
  }

  if (current_bits > kMaxHuffmanPaddingBits || !padding_all_ones)
    return false;

  return true;
}

void EncodeHpackStringLiteral(std::string_view value, bool use_huffman,
                              std::vector<uint8_t>& output) {
  if (use_huffman) {
    std::vector<uint8_t> encoded;
    EncodeHpackHuffman(value, encoded);
    EncodeHpackInteger(encoded.size(), kStringLengthPrefixBits,
                       kStringHuffmanFlag, output);
    output.insert(output.end(), encoded.begin(), encoded.end());
    return;
  }

  EncodeHpackInteger(value.size(), kStringLengthPrefixBits, 0x00, output);
  output.insert(output.end(), value.begin(), value.end());
}

bool DecodeHpackStringLiteral(std::span<const uint8_t> input, size_t& offset,
                              std::string& output) {
  if (offset >= input.size())
    return false;

  bool is_huffman = (input[offset] & kStringHuffmanFlag) != 0;
  uint64_t length = 0;
  if (!DecodeHpackInteger(input, offset, kStringLengthPrefixBits, length))
    return false;

  if (length > input.size() - offset)
    return false;

  std::span<const uint8_t> str_bytes =
      input.subspan(offset, static_cast<size_t>(length));
  offset += static_cast<size_t>(length);

  if (is_huffman)
    return DecodeHpackHuffman(str_bytes, output);

  output.assign(reinterpret_cast<const char*>(str_bytes.data()),
                str_bytes.size());
  return true;
}

const HeaderField* GetHpackStaticTableEntry(size_t index) {
  if (index == 0 || index > kStaticTableSize)
    return nullptr;
  return &GetStaticTable()[index - 1];
}

HpackDynamicTable::HpackDynamicTable(size_t max_size) : max_size_(max_size) {}

void HpackDynamicTable::SetMaxSize(size_t max_size) {
  max_size_ = max_size;
  EvictToSize(max_size_);
}

void HpackDynamicTable::Insert(std::string_view name, std::string_view value) {
  size_t entry_size = name.size() + value.size() + kDynamicEntryOverheadBytes;
  if (entry_size > max_size_) {
    entries_.clear();
    current_size_ = 0;
    return;
  }

  EvictToSize(max_size_ - entry_size);
  entries_.push_front(HeaderField{std::string(name), std::string(value)});
  current_size_ += entry_size;
}

const HeaderField* HpackDynamicTable::GetByCombinedIndex(
    size_t combined_index) const {
  if (combined_index <= kStaticTableSize)
    return nullptr;
  size_t dyn_index = combined_index - kStaticTableSize - 1;
  if (dyn_index >= entries_.size())
    return nullptr;
  return &entries_[dyn_index];
}

size_t HpackDynamicTable::Find(std::string_view name, std::string_view value,
                               bool& exact_match) const {
  exact_match = false;
  size_t name_match_index = 0;
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].name == name) {
      if (entries_[i].value == value) {
        exact_match = true;
        return kStaticTableSize + 1 + i;
      }
      if (name_match_index == 0)
        name_match_index = kStaticTableSize + 1 + i;
    }
  }
  return name_match_index;
}

void HpackDynamicTable::EvictToSize(size_t target_size) {
  while (current_size_ > target_size && !entries_.empty()) {
    const auto& back = entries_.back();
    size_t back_size =
        back.name.size() + back.value.size() + kDynamicEntryOverheadBytes;
    current_size_ =
        (current_size_ >= back_size) ? (current_size_ - back_size) : 0;
    entries_.pop_back();
  }
}

HpackEncoder::HpackEncoder(size_t max_table_size, bool use_huffman)
    : dynamic_table_(max_table_size),
      use_huffman_(use_huffman),
      smallest_pending_table_size_(max_table_size),
      latest_pending_table_size_(max_table_size) {}

void HpackEncoder::SetMaxTableSize(size_t max_table_size) {
  if (!pending_table_size_update_) {
    pending_table_size_update_ = true;
    smallest_pending_table_size_ = max_table_size;
  } else {
    smallest_pending_table_size_ =
        std::min(smallest_pending_table_size_, max_table_size);
  }
  latest_pending_table_size_ = max_table_size;
  dynamic_table_.SetMaxSize(max_table_size);
}

void HpackEncoder::Encode(std::span<const HeaderField> headers,
                          std::vector<uint8_t>& output) {
  if (pending_table_size_update_) {
    if (smallest_pending_table_size_ < latest_pending_table_size_)
      EncodeHpackInteger(smallest_pending_table_size_,
                         kTableSizeUpdatePrefixBits, kTableSizeUpdateFlag,
                         output);
    EncodeHpackInteger(latest_pending_table_size_, kTableSizeUpdatePrefixBits,
                       kTableSizeUpdateFlag, output);
    pending_table_size_update_ = false;
  }

  for (const auto& field : headers) {
    bool allow_indexing = !IsSensitiveHeader(field.name);
    EncodeField(field, allow_indexing, output);
  }
}

void HpackEncoder::EncodeField(const HeaderField& field, bool allow_indexing,
                               std::vector<uint8_t>& output) {
  bool static_exact = false;
  size_t static_idx =
      FindInStaticTable(field.name, field.value, static_exact);
  if (static_exact) {
    EncodeHpackInteger(static_idx, kIndexedPrefixBits, kIndexedHeaderMask,
                       output);
    return;
  }

  bool dyn_exact = false;
  size_t dyn_idx = dynamic_table_.Find(field.name, field.value, dyn_exact);
  if (dyn_exact) {
    EncodeHpackInteger(dyn_idx, kIndexedPrefixBits, kIndexedHeaderMask, output);
    return;
  }

  size_t name_idx = (static_idx != 0) ? static_idx : dyn_idx;
  if (allow_indexing) {
    EncodeHpackInteger(name_idx, kLiteralIncrementalPrefixBits,
                       kLiteralIncrementalFlag, output);
    if (name_idx == 0)
      EncodeHpackStringLiteral(field.name, use_huffman_, output);
    EncodeHpackStringLiteral(field.value, use_huffman_, output);
    dynamic_table_.Insert(field.name, field.value);
    return;
  }

  uint8_t flags =
      IsSensitiveHeader(field.name) ? kLiteralNeverIndexedFlag : 0x00;
  EncodeHpackInteger(name_idx, kLiteralUnindexedPrefixBits, flags, output);
  if (name_idx == 0)
    EncodeHpackStringLiteral(field.name, use_huffman_, output);
  EncodeHpackStringLiteral(field.value, use_huffman_, output);
}

HpackDecoder::HpackDecoder(size_t max_allowed_table_size)
    : dynamic_table_(max_allowed_table_size),
      max_allowed_table_size_(max_allowed_table_size) {}

void HpackDecoder::SetMaxAllowedTableSize(size_t max_allowed_table_size) {
  max_allowed_table_size_ = max_allowed_table_size;
  if (dynamic_table_.MaxSize() > max_allowed_table_size_)
    dynamic_table_.SetMaxSize(max_allowed_table_size_);
}

bool HpackDecoder::Decode(std::span<const uint8_t> block,
                          std::vector<HeaderField>& headers) {
  size_t offset = 0;
  bool seen_header_field = false;

  while (offset < block.size()) {
    uint8_t first_byte = block[offset];

    if ((first_byte & kIndexedHeaderMask) != 0) {
      seen_header_field = true;
      uint64_t index = 0;
      if (!DecodeHpackInteger(block, offset, kIndexedPrefixBits, index) ||
          index == 0)
        return false;
      const HeaderField* entry = LookupIndex(static_cast<size_t>(index));
      if (entry == nullptr)
        return false;
      headers.push_back(*entry);
      continue;
    }

    if ((first_byte & kLiteralIncrementalMask) == kLiteralIncrementalFlag) {
      seen_header_field = true;
      uint64_t name_index = 0;
      if (!DecodeHpackInteger(block, offset, kLiteralIncrementalPrefixBits,
                              name_index))
        return false;
      std::string name;
      if (name_index > 0) {
        const HeaderField* entry = LookupIndex(static_cast<size_t>(name_index));
        if (entry == nullptr)
          return false;
        name = entry->name;
      } else if (!DecodeHpackStringLiteral(block, offset, name)) {
        return false;
      }

      std::string value;
      if (!DecodeHpackStringLiteral(block, offset, value))
        return false;

      dynamic_table_.Insert(name, value);
      headers.push_back(HeaderField{std::move(name), std::move(value)});
      continue;
    }

    if ((first_byte & kTableSizeUpdateMask) == kTableSizeUpdateFlag) {
      if (seen_header_field)
        return false;
      uint64_t new_max_size = 0;
      if (!DecodeHpackInteger(block, offset, kTableSizeUpdatePrefixBits,
                              new_max_size))
        return false;
      if (new_max_size > max_allowed_table_size_)
        return false;
      dynamic_table_.SetMaxSize(static_cast<size_t>(new_max_size));
      continue;
    }

    if ((first_byte & kLiteralUnindexedMask) == 0x00 ||
        (first_byte & kLiteralUnindexedMask) == kLiteralNeverIndexedFlag) {
      seen_header_field = true;
      uint64_t name_index = 0;
      if (!DecodeHpackInteger(block, offset, kLiteralUnindexedPrefixBits,
                              name_index))
        return false;
      std::string name;
      if (name_index > 0) {
        const HeaderField* entry = LookupIndex(static_cast<size_t>(name_index));
        if (entry == nullptr)
          return false;
        name = entry->name;
      } else if (!DecodeHpackStringLiteral(block, offset, name)) {
        return false;
      }

      std::string value;
      if (!DecodeHpackStringLiteral(block, offset, value))
        return false;

      headers.push_back(HeaderField{std::move(name), std::move(value)});
      continue;
    }

    return false;
  }

  return true;
}

const HeaderField* HpackDecoder::LookupIndex(size_t index) const {
  if (index == 0)
    return nullptr;
  if (index <= kStaticTableSize)
    return GetHpackStaticTableEntry(index);
  return dynamic_table_.GetByCombinedIndex(index);
}

}  // namespace http
}  // namespace perception
