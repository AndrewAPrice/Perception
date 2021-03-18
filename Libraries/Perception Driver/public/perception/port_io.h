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

// Port IO functions. The program calling this must be a driver, otherwise
// the caller will terminate!

// Reads a 8-bit value from a port.
uint8 Read8BitsFromPort(uint16 port);

// Reads a signed 8-bit value from a port.
int8 ReadSigned8BitsFromPort(uint16 port);

// Writes an 8-bit value to a port.
void Write8BitsToPort(uint16 port, uint8 data);

// Reads a 16-bit value from a port.
uint16 Read16BitsFromPort(uint16 port);

// Writes a 16-bit value to a port.
void Write16BitsToPort(uint16 port, uint16 data);

// Reads a 32-bit value from a port.
uint32 Read32BitsFromPort(uint16 port);

// Writes a 32-bit value to a port.
void Write32BitsToPort(uint16 port, uint32 data);

}
