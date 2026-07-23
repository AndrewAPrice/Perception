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

#include "types.h"

constexpr uint32 kSataSigAta = 0x00000101;       // SATA drive
constexpr uint32 kSataSigAtapi = 0xEB140101;     // SATAPI drive
constexpr uint32 kSataSigSemb = 0xC33C0101;      // Enclosure management bridge
constexpr uint32 kSataSigPortMult = 0x96690101;  // Port multiplier

constexpr uint32 kAhciPortCmdSt = (1 << 0);
constexpr uint32 kAhciPortCmdFre = (1 << 4);
constexpr uint32 kAhciPortCmdFr = (1 << 14);
constexpr uint32 kAhciPortCmdCr = (1 << 15);

constexpr uint8 kFisTypeRegH2D = 0x27;

constexpr uint8 kAtaCmdReadDmaExt = 0x25;
constexpr uint8 kAtaCmdWriteDmaExt = 0x35;
constexpr uint8 kAtaCmdIdentifyDevice = 0xEC;

struct HbaPort {
  volatile uint32 clb;       // 0x00, Command List Base Address, 1K-byte aligned
  volatile uint32 clbu;      // 0x04, Command List Base Address Upper 32 Bits
  volatile uint32 fb;        // 0x08, FIS Base Address, 256-byte aligned
  volatile uint32 fbu;       // 0x0C, FIS Base Address Upper 32 Bits
  volatile uint32 is;        // 0x10, Interrupt Status
  volatile uint32 ie;        // 0x14, Interrupt Enable
  volatile uint32 cmd;       // 0x18, Command and Status
  volatile uint32 rsv0;      // 0x1C, Reserved
  volatile uint32 tfd;       // 0x20, Task File Data
  volatile uint32 sig;       // 0x24, Signature
  volatile uint32 ssts;      // 0x28, SATA Status (SCR0:SStatus)
  volatile uint32 sctl;      // 0x2C, SATA Control (SCR2:SControl)
  volatile uint32 serr;      // 0x30, SATA Error (SCR1:SError)
  volatile uint32 sact;      // 0x34, SATA Active (SCR3:SActive)
  volatile uint32 ci;        // 0x38, Command Issue
  volatile uint32 sntf;      // 0x3C, SATA Notification (SCR4:SNotification)
  volatile uint32 fbs;       // 0x40, FIS-based Switching Control
  volatile uint32 rsv1[11];  // 0x44 ~ 0x6F, Reserved
  volatile uint32 vendor[4];  // 0x70 ~ 0x7F, Vendor specific
};

struct HbaMem {
  // 0x00 - 0x2B, Generic Host Control
  volatile uint32 cap;        // 0x00, Host Capability
  volatile uint32 ghc;        // 0x04, Global Host Control
  volatile uint32 is;         // 0x08, Interrupt Status
  volatile uint32 pi;         // 0x0C, Ports Implemented
  volatile uint32 vs;         // 0x10, Version
  volatile uint32 ccc_ctl;    // 0x14, Command Completion Coalescing Control
  volatile uint32 ccc_ports;  // 0x18, Command Completion Coalescing Ports
  volatile uint32 em_loc;     // 0x1C, Enclosure Management Location
  volatile uint32 em_ctl;     // 0x20, Enclosure Management Control
  volatile uint32 cap2;       // 0x24, Host Capabilities Extended
  volatile uint32 bohc;       // 0x28, BIOS/OS Handoff Control and Status

  // 0x2C - 0x9F, Reserved
  uint8 rsv[0xA0 - 0x2C];

  // 0xA0 - 0xFF, Vendor Specific registers
  uint8 vendor[0x100 - 0xA0];

  // 0x100 - 0x10FF, Port control registers
  HbaPort ports[32];
};

struct HbaCmdHeader {
  // DW0
  uint8 cfl : 5;  // Command FIS length in DWORDs, 2 ~ 16
  uint8 a : 1;    // ATAPI
  uint8 w : 1;    // Write, 1: H2D, 0: D2H
  uint8 p : 1;    // Prefetchable

  uint8 r : 1;     // Reset
  uint8 b : 1;     // BIST
  uint8 c : 1;     // Clear busy upon R_OK
  uint8 rsv0 : 1;  // Reserved
  uint8 pmp : 4;   // Port multiplier port

  uint16 prdtl;  // Physical region descriptor table length in entries

  // DW1
  uint32 prdbc;  // Physical region descriptor byte count transferred

  // DW2, 3
  uint32 ctba;   // Command table descriptor base address
  uint32 ctbau;  // Command table descriptor base address upper 32 bits

  // DW4 - 7
  uint32 rsv1[4];  // Reserved
};

struct HbaPrdtEntry {
  uint32 dba;       // Data base address
  uint32 dbau;      // Data base address upper 32 bits
  uint32 rsv0;      // Reserved
  uint32 dbc : 22;  // Byte count, 4M max (0-indexed: length - 1)
  uint32 rsv1 : 9;  // Reserved
  uint32 i : 1;     // Interrupt on completion
};

struct HbaCmdTbl {
  // 0x00
  uint8 cfis[64];  // Command FIS

  // 0x40
  uint8 acmd[16];  // ATAPI command, 12 or 16 bytes

  // 0x50
  uint8 rsv[48];  // Reserved

  // 0x80
  HbaPrdtEntry prdt_entry[1];  // Physical region descriptor table entries
};

struct FisRegH2D {
  uint8 fis_type;  // FIS_TYPE_REG_H2D (0x27)
  uint8 pmport_c;  // Port multiplier (bit 0-3), Reserved (bit 4-6), Command
                   // (bit 7: 1=Command, 0=Control)
  uint8 command;   // Command register
  uint8 featurel;  // Feature register low

  uint8 lba0;    // LBA low 0:7
  uint8 lba1;    // LBA mid 8:15
  uint8 lba2;    // LBA high 16:23
  uint8 device;  // Device register (bit 6 = 1 for LBA mode)

  uint8 lba3;      // LBA 24:31
  uint8 lba4;      // LBA 32:39
  uint8 lba5;      // LBA 40:47
  uint8 featureh;  // Feature register high

  uint8 countl;   // Sector count low
  uint8 counth;   // Sector count high
  uint8 icc;      // Isochronous command completion
  uint8 control;  // Control register

  uint8 rsv1[4];  // Reserved
};
