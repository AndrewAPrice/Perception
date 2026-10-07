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

#include <array>
#include <span>

#include "tcp_options.h"
#include "tcp_sequence.h"
#include "tcp_time_wait.h"
#include "tcp_validation.h"
#include "types.h"

// A 128-bit SipHash key.
using SipHashKey = std::array<uint8, 16>;

// Computes SipHash-2-4 (Aumasson and Bernstein) of `data` under `key`.
uint64 SipHash24(const SipHashKey& key, std::span<const uint8> data);

// Generates TCP initial sequence numbers per RFC 6528:
// ISN = M + F(local address, local port, remote address, remote port, secret),
// where M is a timer ticking every 4 microseconds and F is SipHash-2-4
// truncated to 32 bits. ISNs for one 4-tuple increase with time, while
// different 4-tuples get unrelated offsets.
class TcpIsnGenerator {
 public:
  // Creates a generator keyed by `secret`. The service fills the secret from
  // perception::RandomNumber() once at startup.
  explicit TcpIsnGenerator(const SipHashKey& secret);

  // Returns the ISN for a connection. Addresses are the raw address bytes
  // (4 for IPv4, 16 for IPv6), and `now` is the current time.
  uint32 Generate(std::span<const uint8> local_address, uint16 local_port,
                  std::span<const uint8> remote_address, uint16 remote_port,
                  TcpTime now) const;

 private:
  // Secret key for F.
  SipHashKey secret_;
};
