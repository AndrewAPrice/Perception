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

#include "dns.h"

#include <chrono>
#include <cstring>
#include <memory>
#include <vector>

#include "endian.h"
#include "interface.h"
#include "perception/fibers.h"
#include "perception/time.h"
#include "protocols.h"
#include "socket.h"

using ::perception::network::IpAddress;
using ::perception::network::ResolveHostResponse;

namespace {

struct DnsQuery {
  uint16 transaction_id;
  ::perception::Fiber* fiber;
  bool success;
  IpAddress resolved_address;
};

std::vector<DnsQuery> pending_dns_queries;
uint16 next_dns_id = 1;
::perception::devices::MacAddress dns_mac;
bool dns_mac_resolved = false;

}  // namespace

uint16 GetNextDnsId() { return next_dns_id++; }

bool IsDnsMacResolved() { return dns_mac_resolved; }

const ::perception::devices::MacAddress& GetDnsMac() { return dns_mac; }

void SetDnsMac(const ::perception::devices::MacAddress& mac) {
  dns_mac = mac;
  dns_mac_resolved = true;
}

StatusOr<ResolveHostResponse> PerformDnsResolution(const std::string& host) {
  ResolveHostResponse response;
  uint16 tx_id = GetNextDnsId();

  DnsHeader dns;
  dns.id = Swap16BitEndian(tx_id);
  dns.flags = Swap16BitEndian(0x0100);
  dns.questions = Swap16BitEndian(1);
  dns.answers = 0;
  dns.authority = 0;
  dns.additional = 0;

  std::string dns_payload((const char*)&dns, sizeof(dns));

  size_t start = 0;
  while (start < host.length()) {
    size_t end = host.find('.', start);
    if (end == std::string::npos) end = host.length();
    uint8 label_len = (uint8)(end - start);
    dns_payload.push_back((char)label_len);
    dns_payload.append(host.substr(start, label_len));
    start = end + 1;
  }
  dns_payload.push_back('\0');

  uint16 qtype = Swap16BitEndian(1);
  dns_payload.append((const char*)&qtype, 2);

  uint16 qclass = Swap16BitEndian(1);
  dns_payload.append((const char*)&qclass, 2);

  uint32 dns_server_ip = 0x08080808;
  uint16 dns_src_port = 50053;

  DnsQuery query;
  query.transaction_id = tx_id;
  query.fiber = ::perception::GetCurrentlyExecutingFiber();
  query.success = false;

  pending_dns_queries.push_back(query);

  auto current_fiber = ::perception::GetCurrentlyExecutingFiber();
  bool got_reply = false;

  for (int attempt = 0; attempt < 3; attempt++) {
    SendUdpPacket(0, dns_server_ip, dns_src_port, 53, dns_payload);

    struct AttemptState {
      bool finished = false;
    };
    auto attempt_state = std::make_shared<AttemptState>();

    auto timeout_fiber =
        ::perception::Fiber::Create([current_fiber, attempt_state]() {
          ::perception::SleepForDuration(std::chrono::milliseconds(1500));
          if (!attempt_state->finished) {
            current_fiber->WakeUp();
          }
        });
    timeout_fiber->WakeUp();

    ::perception::Sleep();
    attempt_state->finished = true;

    for (const auto& q : pending_dns_queries) {
      if (q.transaction_id == tx_id && q.success) {
        got_reply = true;
        break;
      }
    }
    if (got_reply) {
      break;
    }
  }

  bool success = false;
  for (auto it = pending_dns_queries.begin(); it != pending_dns_queries.end();
       ++it) {
    if (it->transaction_id == tx_id) {
      success = it->success;
      if (it->success) {
        response.addresses.push_back(it->resolved_address);
      }
      pending_dns_queries.erase(it);
      break;
    }
  }

  if (!success) return ::perception::Status::INTERNAL_ERROR;
  return response;
}

void ProcessDnsResponse(const uint8* payload, size_t len) {
  if (len < sizeof(DnsHeader)) return;

  const DnsHeader* dns = (const DnsHeader*)payload;
  uint16 id = Swap16BitEndian(dns->id);
  uint16 answers_count = Swap16BitEndian(dns->answers);

  for (auto& query : pending_dns_queries) {
    if (query.transaction_id == id) {
      if (answers_count > 0) {
        size_t offset = sizeof(DnsHeader);
        while (offset < len) {
          uint8 label_len = payload[offset];
          if (label_len == 0) {
            offset++;
            break;
          }
          offset += 1 + label_len;
        }
        offset += 4;  // QType, QClass

        for (uint16 a = 0; a < answers_count && offset < len; a++) {
          if ((payload[offset] & 0xC0) == 0xC0) {
            offset += 2;
          } else {
            while (offset < len) {
              uint8 label_len = payload[offset];
              if (label_len == 0) {
                offset++;
                break;
              }
              offset += 1 + label_len;
            }
          }

          if (offset + 10 > len) break;

          uint16 type = (payload[offset] << 8) | payload[offset + 1];
          uint16 data_len = (payload[offset + 8] << 8) | payload[offset + 9];
          offset += 10;

          if (type == 1 && data_len == 4) {
            if (offset + 4 <= len) {
              query.resolved_address.address[0] = payload[offset];
              query.resolved_address.address[1] = payload[offset + 1];
              query.resolved_address.address[2] = payload[offset + 2];
              query.resolved_address.address[3] = payload[offset + 3];
              query.success = true;
              break;
            }
          }
          offset += data_len;
        }
      }
      query.fiber->WakeUp();
      break;
    }
  }
}
