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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "perception/network/ip_address.h"

// DNS resource record types used by the resolver.
enum class DnsRecordType : uint16 { A = 1, Cname = 5, Soa = 6, Aaaa = 28 };

// DNS response codes (RFC 1035 section 4.1.1). Other values may appear.
enum class DnsResponseCode : uint8 {
  NoError = 0,
  FormatError = 1,
  ServerFailure = 2,
  NameError = 3,
  NotImplemented = 4,
  Refused = 5
};

// A resolved address and its time to live.
struct DnsAddressRecord {
  // The A or AAAA address.
  ::perception::network::IpAddress address;
  // Time to live in seconds (the minimum along any CNAME chain).
  uint32 ttl = 0;
};

// The question section of a DNS message.
struct DnsQuestion {
  // Canonical (lowercase, no trailing dot) query name.
  std::string name;
  // Raw query type.
  uint16 type = 0;
  // Raw query class.
  uint16 query_class = 0;
};

// A decoded DNS response.
struct DnsResponse {
  // Transaction ID.
  uint16 id = 0;
  // True if the TC (truncated) bit was set.
  bool truncated = false;
  // Response code from the header.
  DnsResponseCode response_code = DnsResponseCode::NoError;
  // The question, if the response carried one.
  std::optional<DnsQuestion> question;
  // Canonical name after following CNAMEs from the question name.
  std::string canonical_name;
  // Addresses owned by the canonical name that match the question type.
  std::vector<DnsAddressRecord> addresses;
  // Negative caching TTL from an authority SOA record: min(SOA TTL, SOA
  // MINIMUM) per RFC 2308 section 5.
  std::optional<uint32> negative_ttl;
};

// Returns `name` lowercased with a single trailing dot removed.
std::string CanonicalDnsName(std::string_view name);

// Encodes a recursive (RD=1) query for `name` of `type`. Returns nullopt if
// the name is empty or has a label longer than 63 bytes, an empty label, or an
// encoded length above 255 bytes.
std::optional<std::string> EncodeDnsQuery(uint16 id, std::string_view name,
                                          DnsRecordType type);

// Decodes a DNS response. Every length is bounds-checked, compression
// pointers are followed with a limit of 16 jumps, and CNAME chains in the
// answer section are followed from the question name. Returns nullopt for
// malformed packets, queries (QR=0), or more than one question.
std::optional<DnsResponse> DecodeDnsResponse(std::string_view packet);

// Returns true if `response` answers the query identified by `id`, `name`
// and `type` (matching ID, class IN, type, and case-insensitive name).
bool IsResponseToQuery(const DnsResponse& response, uint16 id,
                       std::string_view name, DnsRecordType type);
