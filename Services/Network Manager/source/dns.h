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

#include <string>

#include "perception/network/network_service.h"
#include "status.h"

// Returns the next unique transaction ID identifier to utilize when composing a
// new DNS query.
uint16 GetNextDnsId();

// Composes, transmits, and waits for dual-stack DNS query resolution targeting
// the specified host and optional address family filter.
StatusOr<::perception::network::ResolveHostResponse> PerformDnsResolution(
    const std::string& host,
    ::perception::network::IpAddressFamily family =
        ::perception::network::IpAddressFamily::Unspecified);

// Parses a received DNS response packet, caches the A/AAAA records or negative
// response, matches it to the pending query transaction, and wakes the
// initiating fiber.
void ProcessDnsResponse(const uint8* payload, size_t len);
