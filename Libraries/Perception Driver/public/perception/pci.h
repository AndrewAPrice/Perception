// Copyright 2020 Google LLC
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

#include "types.h"

namespace perception {

constexpr uint8 kPciHdrVendorId = 0;
constexpr uint8 kPciHdrDeviceId = 2;
constexpr uint8 kPciHdrCommand = 4;
constexpr uint8 kPciHdrStatus = 6;
constexpr uint8 kPciHdrRevisionId = 8;
constexpr uint8 kPciHdrProgIf = 9;
constexpr uint8 kPciHdrSubclass = 10;
constexpr uint8 kPciHdrClassCode = 11;
constexpr uint8 kPciHdrCacheLineSize = 12;
constexpr uint8 kPciHdrLatencyTimer = 13;
constexpr uint8 kPciHdrHeaderType = 14;
constexpr uint8 kPciHdrBist = 15;
constexpr uint8 kPciHdrBar0 = 16;
constexpr uint8 kPciHdrBar1 = 20;
constexpr uint8 kPciHdrBar2 = 24;
constexpr uint8 kPciHdrBar3 = 28;
constexpr uint8 kPciHdrBar4 = 32;
constexpr uint8 kPciHdrBar5 = 36;
constexpr uint8 kPciHdrSecondaryBusNumber = 25;


uint8 Read8BitsFromPciConfig(uint8 bus, uint8 slot, uint8 func, uint8 offset);

uint16 Read16BitsFromPciConfig(uint8 bus, uint8 slot, uint8 func, uint8 offset);

uint32 Read32BitsFromPciConfig(uint8 bus, uint8 slot, uint8 func, uint8 offset);

}