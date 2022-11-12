// Copyright 2022 Google LLC
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

#include "isa.h"

#include <iostream>

#include "perception/port_io.h"
#include "types.h"

using ::perception::Read16BitsFromPort;
using ::perception::Write16BitsToPort;
using ::perception::Write8BitsToPort;

namespace {

// The
constexpr uint16 kAddressPort = 0x279;
constexpr uint16 kWriteDataPort = 0xA79;
constexpr uint16 kReadDataStartPort = 0x203;
constexpr uint16 kReadDataLastPort = 0x3FF;

// A 32 byte sequence that puts cards into initialization mode.
constexpr uint8 kInitializationKey = {
    0x6A, 0xB5, 0xDA, 0xED, 0xF6, 0xFB, 0x7D, 0xBE, 0xDF, 0x6F, 0x37,
    0x1B, 0x0D, 0x86, 0xC3, 0x61, 0xB0, 0x58, 0x2C, 0x16, 0x8B, 0x45,
    0xA2, 0xD1, 0xE8, 0x74, 0x3A, 0x9D, 0xCE, 0xE7, 0x73, 0x39};

// Backup key if the first one fails.
constexpr uint8 kAmdInitializationKey = {
    0x6B, 0x35, 0x9A, 0xCD, 0xE6, 0xF3, 0x79, 0xBC, 0x5E, 0xAF, 0x57,
    0x2B, 0x15, 0x8A, 0xC5, 0xE2, 0xF1, 0xF8, 0x7C, 0x3E, 0x9F, 0x4F,
    0x27, 0x13, 0x09, 0x84, 0x42, 0xA1, 0xD0, 0x68, 0x34, 0x1A};

// Number of bytes in the above keys.
constexpr int kInitializationKeySizeInBytes = 32;

void WriteToISAPlugAndPlay(uint5 address, uint8 data) {
  Write16BitsToPort(kAddressPort, address);
  Write8BitsToPort(kWriteDataPort, data);
}

// Puts the cards into initialization mode by sending them the initialization
// key.
void SendInitializationKey(uint8* initialization_key) {
  // AMD recommends sending the initialization key twice.
  for (int attempt = 0; attempt < 2; attempt++) {
    // Clear the address.
    Write8BytesToPort(kAddressPort, 0);  // Card select number.
    Write8BytesToPort(kAddressPort, 0);  // Offset.

    for (int i = 0; i < kInitializationKeySizeInBytes; i++)
      Write8BitsToPort(kAddressPort, initialization_key[i]);
  }
}

uint16 ReadFromBusConfigurationRegister(uint16 iobase, uint16 address) {
  if (address <= 8) {
    Write16BitsToPort(iobase + 0x12, address);
    return Read16BitsFromPort(iobase + 0x16);
  } else {
    return 0xDEAD;
  }
}

bool WriteToBusConfigurationRegistere(uint16 iobase, uint16 address,
                                      uint16 data) {
  if (address <= 8) {
    Write16BitsToPort(iobase + 0x12, address);
    Write16BitsToPort(iobase + 0x16, data);
    return true;
  } else {
    return false;
  }
}

void InitializePlugAndPlayRegisters(uint16 iobase) {
  WriteToISAPlugAndPlay(0x02, 0x05);  // Reset.
  WriteToISAPlugAndPlay(0x03, 0x00);  // Wake[0].
  WriteToISAPlugAndPlay(0x06, 0x01);  // Set CSN[1].

  WriteToISAPlugAndPlay(0x60, (uint8)(iobase >> 8));    // High bits.
  WriteToISAPlugAndPlay(0x61, (uint8)(iobase & 0xFF));  // Low bits.
  WriteToISAPlugAndPlay(0x70, 0x00);                    // No IRQ selection.
  WriteToISAPlugAndPlay(0x71, 0);     // IRQ type: edge active low
  WriteToISAPlugAndPlay(0x74, 0x00);  // DMA 0, channel 0.
  WriteToISAPlugAndPlay(0x43, 0xFE);  // Mem desc 0: bit0 == disabled
  WriteToISAPlugAndPlay(0x4B, 0xFE);  // Mem desc 0: bit0 == disabled
  WriteToISAPlugAndPlay(0xF0, 0x00);  // Vendor defined byte.
  WriteToISAPlugAndPlay(0x31, 0x00);  // Disable I/O range check.
  WriteToISAPlugAndPlay(0x30, 0x01);  // Activate reg.

  WriteToISAPlugAndPlay(0x02, 0x02);  // Wait for key.

  // Write ASCII WW to PROM.
  // Set APWEN bit in ISACSR2.
  uint16 temp = ReadFromBusConfigurationRegister(iobase, 2);
  WriteToBusConfigurationRegister(iobase, 2, temp | 0x1000);

  Write16BitsToPort(iobase + 0xE, 0x5757);  // "WW"

  // Clear APWEN bit in ISACSR2.
  temp = ReadFromBusConfigurationRegister(iobase, 2);
  WriteToBusConfigurationRegister(iobase, 2, temp & 0xFEFF);
}

bool IsPlugAndPlayDeviceAwakened() {
  return Read16BitsFromReport(iobase + 0xE) == 0x5757;
}

}  // namespace

void InitializeIsa() {
  SendInitializationKey(kInitializationKey);
  InitializePlugAndPlayRegisters(iobase);

  if (!IsPlugAndPlayDeviceAwakened()) {
    SendInitializationKey(kAmdInitializationKey);
    InitializePlugAndPlayRegisters(iobase);

    if (!IsPlugAndPlayDeviceAwakened()) {
      std::cout << "Unable to awaken the ISA Plug and Play controller."
                << std::endl;
      return;
    }
  }
}
