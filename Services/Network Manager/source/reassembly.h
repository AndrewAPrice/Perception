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

#include <chrono>
#include <compare>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "perception/network/ip_address.h"

// Identifies an IPv4 or IPv6 datagram being reassembled.
struct ReassemblyKey {
  // Address family (V4 or V6).
  ::perception::network::IpAddressFamily family =
      ::perception::network::IpAddressFamily::Unspecified;
  // Source IP address.
  ::perception::network::IpAddress source;
  // Destination IP address.
  ::perception::network::IpAddress destination;
  // Fragment identification (16-bit for IPv4, 32-bit for IPv6).
  uint32 identification = 0;
  // Upper-layer protocol (IPv4 protocol field, or IPv6 Fragment Next Header).
  uint8 protocol = 0;

  bool operator==(const ReassemblyKey& other) const = default;
  std::strong_ordering operator<=>(const ReassemblyKey& other) const = default;
};

// Outcome of feeding a fragment to the reassembler.
enum class ReassemblyStatus : uint8 {
  // More fragments are still needed to complete the datagram.
  Incomplete,
  // All fragments have arrived; `payload` holds the reassembled payload.
  Complete,
  // An overlapping fragment was detected (RFC 5722); the datagram was dropped.
  DroppedOverlap,
  // The first IPv6 fragment did not contain the complete header chain through
  // the upper-layer header (RFC 7112), or carried fragmented NDP (RFC 6980);
  // the datagram was dropped.
  IncompleteFirstFragmentHeader,
  // The fragment or reassembled datagram exceeds the 64 KiB limit.
  DatagramTooLarge,
  // The fragment offset, length, or alignment is malformed.
  InvalidFragment,
};

// Result of processing a single fragment.
struct ReassemblyResult {
  // Outcome status.
  ReassemblyStatus status = ReassemblyStatus::InvalidFragment;
  // Upper-layer protocol of the completed datagram (taken from the first
  // fragment).
  uint8 protocol = 0;
  // Reassembled upper-layer payload when `status == Complete`.
  std::string payload;
};

// Information about a timed-out reassembly buffer returned by Purge().
struct ExpiredReassembly {
  // Key of the expired datagram.
  ReassemblyKey key;
  // True if fragment 0 had arrived (required to send ICMP Time Exceeded code 1
  // per RFC 792 / RFC 4443 section 3.3).
  bool had_first_fragment = false;
  // Payload of the first fragment (or empty if fragment 0 never arrived).
  std::string first_fragment_payload;
};

// Shared IPv4 and IPv6 fragment reassembler (RFC 791, RFC 8200 section 4.5,
// RFC 5722, RFC 6946, RFC 7112).
class Reassembler {
 public:
  // Processes one fragment for `key` at byte `offset` (in bytes, already
  // multiplied by 8) with `more_fragments` (M flag) and `data`.
  // `unfragmentable_length` is the size of any IPv6 headers preceding the
  // Fragment header that will be part of the reassembled payload check.
  ReassemblyResult AddFragment(const ReassemblyKey& key, uint16 offset,
                               bool more_fragments, std::string_view data,
                               std::chrono::steady_clock::time_point now,
                               size_t unfragmentable_length = 0);

  // Removes all reassembly buffers whose timeout (30 s for IPv4, 60 s for
  // IPv6) has expired at `now`, returning metadata so the caller can emit
  // rate-limited ICMP Time Exceeded messages when fragment 0 was present.
  std::vector<ExpiredReassembly> Purge(
      std::chrono::steady_clock::time_point now);

  // Returns the number of datagrams currently awaiting reassembly.
  size_t ActiveDatagrams() const { return buffers_.size(); }

 private:
  // One non-overlapping contiguous fragment piece stored in offset order.
  struct Piece {
    // Byte offset from the start of the fragmentable payload.
    uint16 offset = 0;
    // Fragment bytes.
    std::string data;
  };

  // State of a single datagram being reassembled.
  struct Buffer {
    // Sorted, non-overlapping pieces received so far.
    std::vector<Piece> pieces;
    // Sum of `piece.data.size()` across `pieces`.
    size_t received_bytes = 0;
    // Total fragmentable payload length once the final fragment (M=0) arrives.
    std::optional<size_t> total_length;
    // Size of the unfragmentable headers recorded from fragment 0.
    size_t unfragmentable_length = 0;
    // Upper-layer protocol from fragment 0.
    uint8 first_fragment_protocol = 0;
    // True once fragment 0 (offset == 0) has arrived.
    bool has_first_fragment = false;
    // Copy of fragment 0's payload for ICMP Time Exceeded quoting.
    std::string first_fragment_payload;
    // Deadline at which this buffer expires.
    std::chrono::steady_clock::time_point expires;
  };

  // Ensures `buffers_.size() < kMaxConcurrentDatagrams` by purging expired
  // entries and, if still full, evicting the soonest-expiring buffer.
  void MakeRoom(std::chrono::steady_clock::time_point now);

  // Active reassembly buffers keyed by datagram identity.
  std::map<ReassemblyKey, Buffer> buffers_;
};
