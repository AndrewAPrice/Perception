// Copyright 2021 Google LLC
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

#include "pci_device_names.h"

std::string GetPciDeviceName(uint8 base_class, uint8 sub_class, uint8 prog_if) {
	switch(base_class) {
		case 0x00: switch(sub_class) {
				default: return "Unknown Device";
				case 0x00: return "Non-VGA-Compatible Device";
				case 0x01: return "VGA-Compatible Device";
			}
		case 0x01: switch(sub_class) {
				default: return "Unknown Mass Storage Controller";
				case 0x00: return "SCSI Bus Controller";
				case 0x01: switch (prog_if) {
						default: return "IDE Controller";
						case 0x00: return "ISA Compatibility mode-only IDE controller";
						case 0x05: return "PCI native mode-only IDE controller";
						case 0x0A: return "ISA Compatibility mode IDE controller, supports both channels switched to PCI native mode";
						case 0x0F: return "PCI native mode IDE controller, supports both channels switched to ISA compatibility mode";
						case 0x80: return "ISA Compatibility mode-only IDE controller, supports bus mastering";
						case 0x85: return "PCI native mode-only IDE controller, supports bus mastering";
						case 0x8A: return "ISA Compatibility mode IDE controller, supports both channels switched to PCI native mode, supports bus mastering";
						case 0x8F: return "PCI native mode IDE controller, supports both channels switched to ISA compatibility mode, supports bus mastering";
					}
				case 0x02: return "Floppy Disk Controller";
				case 0x03: return "IPI Bus Controller";
				case 0x04: return "RAID Controller";
				case 0x05: switch (prog_if) {
						default: return "ATA Controller";
						case 0x20: return "Single DMA (ATA Controller)";
						case 0x30: return "Chained DMA (ATA Controller)";
					}
				case 0x06: switch (prog_if) {
						default: return "Serial ATA";
						case 0x00: return "Vendor Specific Interface (Serial ATA)";
						case 0x01: return "AHCI 1.0 (Serial ATA)";
						case 0x02: return "Serial Storage Bus (Serial ATA)";
					}
				case 0x07: switch (prog_if) {
						default: return "Serial Attached SCSI";
						case 0x00: return "SAS (Serial Attached SCSI)";
						case 0x01: return "Serial Storage Bus (Serial Attached SCSI)";
					}
				case 0x08: switch (prog_if) {
						default: return "Non-Volatile Memory Controller";
						case 0x01: return "NVMHCI (Non-Volatile Memory Controller)";
						case 0x02: return "NVM Express (Non-Volatile Memory Controller)";
					}
			}
		case 0x02: switch(sub_class) {
				default: return "Unknown Network Controller";
				case 0x00: return "Ethernet Controller";
				case 0x01: return "Token Ring Controller";
				case 0x02: return "FDDI Controller";
				case 0x03: return "ATM Controller";
				case 0x04: return "ISDN Controller";
				case 0x05: return "WorldFip Controller";
				case 0x06: return "PICMG 2.14 Multi Computing";
				case 0x07: return "Infiniband Controller";
				case 0x08: return "Fabric Controller";
			}
		case 0x03: switch(sub_class) {
				default: return "Unknown Display Controller";
				case 0x00: switch (prog_if) {
						default: return "VGA-Compatible Controller";
						case 0x00: return "VGA Controller";
						case 0x01: return "8514-Compatible Controller";
					}
				case 0x01: return "XGA Controller";
				case 0x02: return "3D Controller (Not VGA-Compatible)";
			}
		case 0x04: switch(sub_class) {
				default: return "Unknown Multimedia Controller";
				case 0x00: return "Multimedia Video Controller";
				case 0x01: return "Multimedia Audio Controller";
				case 0x02: return "Computer Telephony Device";
				case 0x03: return "Audio Device";
			}
		case 0x05: switch(sub_class) {
				default: return "Unknown Memory Controller";
				case 0x00: return "RAM Controller";
				case 0x01: return "Flash Controller";
			}
		case 0x06: switch(sub_class) {
				default: return "Unknown Bridge Device";
				case 0x00: return "Host Bridge";
				case 0x01: return "ISA Bridge";
				case 0x02: return "EISA Bridge";
				case 0x03: return "MCA Bridge";
				case 0x04: switch (prog_if) {
						default: return "PCI-to-PCI Bridge";
						case 0x00: return "Normal Decode (PCI-to-PCI Bridge)";
						case 0x01: return "Subtractive Decode (PCI-to-PCI Bridge)";
					}
				case 0x05: return "PCMCIA Bridge";
				case 0x06: return "NuBus Bridge";
				case 0x07: return "CardBus Bridge";
				case 0x08:  switch (prog_if) {
						default: return "RACEway Bridge";
						case 0x00: return "Transparent Mode (RACEway Bridge)";
						case 0x01: return "Endpoint Mode (RACEway Bridge)";
					}
				case 0x09: switch (prog_if) {
						default: return "PCI-to-PCI Bridge";
						case 0x40: return "Semi-Transparent PCI-to-PCI Bridge, Primary bus towards host CPU";
						case 0x80: return "Semi-Transparent PCI-to-PCI Bridge, Secondary bus towards host CPU";
					}
				case 0x0A: return "InfiniBrand-to-PCI Host Bridge";
			}
		case 0x07: switch(sub_class) {
				default: return "Unknown Simple Communication Controller";
				case 0x00: switch (prog_if) {
						default: return "Serial Controller";
						case 0x00: return "8250-Compatible (Generic XT) Serial Controller";
						case 0x01: return "16450-Compatible Serial Controller";
						case 0x02: return "16550-Compatible Serial Controller";
						case 0x03: return "16650-Compatible Serial Controller";
						case 0x04: return "16750-Compatible Serial Controller";
						case 0x05: return "16850-Compatible Serial Controller";
						case 0x06: return "16950-Compatible Serial Controller";
					}
				case 0x01: switch (prog_if) {
						default: return "IEEE 1284 or Parallel Port";
						case 0x00: return "Standard Parallel Port";
						case 0x01: return "Bi-Directional Parallel Port";
						case 0x02: return "ECP 1.X Compliant Parallel Port";
						case 0x03: return "IEEE 1284 Controller";
						case 0xFE: return "IEEE 1284 Target Device";
					}
				case 0x02: return "Multiport Serial Controller";
				case 0x03: switch (prog_if) {
						default: return "Modem";
						case 0x00: return "Generic Modem";
						case 0x01: return "Hayes 16450-Compatible Modem";
						case 0x02: return "Hayes 16550-Compatible Modem";
						case 0x03: return "Hayes 16650-Compatible Modem";
						case 0x04: return "Hayes 16750-Compatible Modem";
					}
				case 0x04: return "IEE 488.1/2 Controller";
				case 0x05: return "Smart Card";
			}
		case 0x08: switch(sub_class) {
				default: return "Unknown Base System Peripheral";
				case 0x00: switch (prog_if) {
						default: return "Programmable Interrupt Controller";
						case 0x00: return "Generic 8259-Compatible Programmable Interrupt Controller";
						case 0x01: return "ISA-Compatible Programmable Interrupt Controller";
						case 0x02: return "EISA-Compatible Programmable Interrupt Controller";
						case 0x10: return "I/O APIC Interrupt Controller";
						case 0x20: return "I/O(x) APIC Interrupt Controller";
					}
				case 0x01: switch (prog_if) {
						default: return "DMA Controller";
						case 0x00: return "Generic 8237-Compatible DMA Controller";
						case 0x01: return "ISA-Compatible DMA Controller";
						case 0x02: return "EISA-Compatible DMA Controller";
					}
				case 0x02: switch (prog_if) {
						default: return "Timer";
						case 0x00: return "Generic 8254-Compatible Timer";
						case 0x01: return "ISA-Compatible Timer";
						case 0x02: return "EISA-Compatible Timer";
						case 0x03: return "High Precision Event Timer";
					}
				case 0x03: switch (prog_if) {
						default: return "RTC Controller";
						case 0x00: return "Generic RTC Controller";
						case 0x01: return "ISA-Compatible RTC Controller";
					}
				case 0x04: return "PCI Hot-Plug Controller";
				case 0x05: return "SD Host controller";
				case 0x06: return "IOMMU";
			}
		case 0x09: switch(sub_class) {
				default: return "Unknown Input Device";
				case 0x00: return "Keyboard Controller";
				case 0x01: return "Digitzer";
				case 0x02: return "Mouse Controller";
				case 0x03: return "Scanner Controller";
				case 0x04: switch (prog_if) {
					default: return "Gameport Controller";
					case 0x00: return "Generic Gameport Controller";
					case 0x10: return "Extended Gameport Controller";
				}
			}
		case 0x0A: switch(sub_class) {
				default: return "Unknown Docking Station";
				case 0x00: return "Generic Docking Station";
			}
		case 0x0B: switch(sub_class) {
				default: return "Unknown Processor";
				case 0x00: return "386 Processor";
				case 0x01: return "486 Processor";
				case 0x02: return "Pentium Processor";
				case 0x03: return "Pentium Pro Processor";
				case 0x10: return "Alpha Processor";
				case 0x20: return "PowerPC Processor";
				case 0x30: return "MIPS Processor";
				case 0x40: return "Co-Processor";
			}
		case 0x0C: switch(sub_class) {
				default: return "Unknown Serial Bus Controller";
				case 0x00: switch (prog_if) {
						default: return "FireWire (IEE 1394) Controller";
						case 0x00: return "Generic FireWire (IEE 1394) Controller";
						case 0x01: return "OHCI FireWire (IEE 1394) Controller";
					}
				case 0x01: return "ACCESS.bus";
				case 0x02: return "SSA";
				case 0x03: switch (prog_if) {
						default: return "USB Controller";
						case 0x00: return "UHCI USB Controller";
						case 0x10: return "OHCI Controller";
						case 0x20: return "EHCI (USB2) Controller";
						case 0x30: return "XHCI (USB3) Controller";
						case 0xFe: return "USB Device (Not a host controller)";
					}
				case 0x04: return "Fibre Channel Controller";
				case 0x05: return "SMBus";
				case 0x06: return "InfiniBand";
				case 0x07: switch (prog_if) {
						default: return "IPMI Interface";
						case 0x00: return "SMIC IPMI Interface";
						case 0x01: return "Keyboard Controller Style IPMI Interface";
						case 0x02: return "Block Transfer IPMI Interface";
					}
				case 0x08: return "SERCOS Interface Standard";
				case 0x09: return "CANbus";
			}
		case 0x0D: switch(sub_class) {
				default: return "Unknown Wireless Controller";
				case 0x00: return "iRDA Compatible Controller";
				case 0x01: return "Consumer IR Controller";
				case 0x10: return "RF Controller";
				case 0x11: return "Bluetooth Controller";
				case 0x12: return "Broadband Controller";
				case 0x20: return "Ethernet Controller (802.11a)";
				case 0x21: return "Ethernet Controller (802.11b)";
			}
		case 0x0E: switch(sub_class) {
				default: return "Unknown Intelligent I/O Controller";
				case 0x00: return "I20"; 
			}
		case 0x0F: switch(sub_class) {
				default: return "Unknown Satellite Communication Controller";
				case 0x01: return "Satellite TV Controller";
				case 0x02: return "Satellite Audio Controller";
				case 0x03: return "Satellite Voice Controller";
				case 0x04: return "Satellite Data Controller";
			}
		case 0x10: switch(sub_class) {
				default: return "Unknown Encryption/Decryption Controller";
				case 0x00: return "Network and Computing Encryption/Decryption";
				case 0x10: return "Entertainment Encryption/Decryption";
			}
		case 0x11: switch(sub_class) {
				default: return "Unknown Data Acquistion or Signal Processing Controller";
				case 0x00: return "DPIO Modules";
				case 0x01: return "Performance Counters";
				case 0x10: return "Communication Synchronizer";
				case 0x20: return "Signal Processing Management";
			}
		case 0x12: return "Processing Accelerator";
		case 0x13: return "Non-Essential Instrumentation";
		case 0x40: return "Co-Processor";
		default: return "Unknown Device";
	}
}
