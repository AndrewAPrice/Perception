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
#include <vector>

#include "perception/devices/network_device.h"
#include "perception/fibers.h"
#include "perception/network/network_service.h"

// Returns the next unique transaction ID identifier to utilize when composing a
// new DNS query.
uint16 GetNextDnsId();

// Returns true if the DNS server's MAC address has been resolved via ARP.
bool IsDnsMacResolved();

// Returns the resolved DNS server MAC hardware address.
const ::perception::devices::MacAddress& GetDnsMac();

// Sets the resolved DNS server MAC hardware address and marks it as resolved.
void SetDnsMac(const ::perception::devices::MacAddress& mac);

// Composes, transmits, and waits for DNS query resolution targeting the
// specified host.
StatusOr<::perception::network::ResolveHostResponse> PerformDnsResolution(
    const std::string& host);

// Parses a received DNS response packet, extracts the resolved A-record IP,
// matches it to the pending query transaction, and wakes the initiating fiber.
void ProcessDnsResponse(const uint8* payload, size_t len);
