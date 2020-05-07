#include "pci.h"
#include "io.h"
#include "text_terminal.h"
#include "liballoc.h"
#include "ide.h"
#include "video.h"

uint32 pci_config_read_dword(uint8 bus, uint8 slot, uint8 func, uint8 offset) {
	uint32 lbus = (uint32)bus;
	uint32 lslot = (uint32)slot;
	uint32 lfunc = (uint32)func;

	uint32 address = (uint32)((lbus << 16) | (lslot << 11) |
		(lfunc << 8) | offset | ((uint32)0x80000000));

	/* wroute out the address */
	outportdw(0xCF8, address);

	/* read in the data */
	return inportdw(0xCFC);
}

uint16 pci_config_read_word(uint8 bus, uint8 slot, uint8 func, uint8 offset) {
	uint32 lbus = (uint32)bus;
	uint32 lslot = (uint32)slot;
	uint32 lfunc = (uint32)func;
	
	/* bits:
		31 - enabled bit
		30-24 - reserved
		23-16 - bus number
		15-11 - device number
		10-8 - function number
		7-2 - register number
		1-0 - 00 */

	uint32 address = (uint32)((lbus << 16) | (lslot << 11) |
		(lfunc << 8) | (offset & 0xFC) | ((uint32)0x80000000));

	/* wroute out the address */
	outportdw(0xCF8, address);

	/* read in the data */
	return (uint16)((inportdw(0xCFC) >> (((uint16)offset & 2) * 8)) & 0xFFFF);
}

uint8 pci_config_read_byte(uint8 bus, uint8 slot, uint8 func, uint8 offset) {
	uint16 word = pci_config_read_word(bus, slot, func, offset & 0xFE);
	if(offset & 1)
		return (uint8)(word >> 8);
	else
		return (uint8)(word & 0xFF);
}

/* these functions all have to do with scanning for devices
************************************************************/
void pci_check_device(uint8 bus, uint8 slot);

void pci_check_bus(uint8 bus) {
	uint8 device;
	for(device = 0; device < 32; device++)
		pci_check_device(bus, device);
}

void pci_check_function(uint8 bus, uint8 slot, uint8 function);

void pci_check_device(uint8 bus, uint8 slot) {
	/* check if there is a device here - on function 0 */
	uint16 vendor = pci_config_read_word(bus, slot, 0, PCI_HDR_VENDOR_ID);
	if(vendor == 0xFFFF) return;

	/* check what functions it performs */
	pci_check_function(bus, slot, 0);
	uint16 header_type = pci_config_read_byte(bus, slot, 0, PCI_HDR_HEADER_TYPE);

	if((header_type & 0x80) != 0) { /* multi-function device */
		uint8 function;	for(function = 1; function < 8; function++) {
			if(pci_config_read_word(bus, slot, function, PCI_HDR_VENDOR_ID) != 0xFFFF)
				pci_check_function(bus, slot, function);
		}
	}
}

/* end of the scanning routines
************************************************************/

/* found a device */
void pci_check_function(uint8 bus, uint8 slot, uint8 function) {
	struct PCIDevice *device = malloc(sizeof(struct PCIDevice));
	if(device == 0) return; /* no memory, can't do anything */

	device->bus = bus;
	device->slot = slot;
	device->function = function;

	device->base_class = pci_config_read_byte(bus, slot, function, PCI_HDR_CLASS_CODE);
	device->sub_class = pci_config_read_byte(bus, slot, function, PCI_HDR_SUBCLASS);
	device->vendor_id = pci_config_read_word(bus, slot, function, PCI_HDR_VENDOR_ID);
	device->device_id = pci_config_read_word(bus, slot, function, PCI_HDR_DEVICE_ID);

	device->driver = 0;
	device->next = pci_devices;
	pci_devices = device;

	
	switch(device->base_class) {
		case 0x00: switch(device->sub_class) {
				case 0x01: /* VGA-Compatible Device */
					init_video_device(device);
					break;
			}
		case 0x01: /* mass storage controller */
			switch(device->sub_class) {
				case 0x01: /* IDE controller */
					init_ide(device);
					break;
			} break;
		case 0x03: /* display controller */
			switch(device->sub_class) {
				case 0x00: /* VGA-Compatible Controller */
					init_video_device(device);
					break;
			} break;
		case 0x06: /* bridge device */
			switch(device->sub_class) {
				case 0x04: { /* PCI-to-PCI Bridge */
					device->driver = 1;
					uint8 secondaryBus = pci_config_read_byte(bus, slot, function, PCI_HDR_SECONDARY_BUS_NUMBER);
					pci_check_bus(secondaryBus);
				} break;
			} break;
	}
}


struct PCIDevice *pci_devices;

void init_pci() {
	pci_devices = 0;

	/* scan buses for devices */
	uint16 header_type = pci_config_read_word(0, 0, 0, PCI_HDR_VENDOR_ID);
	if((header_type & 0x80) == 0)
		/* single pci host controller */
		pci_check_bus(0);
	else {
		/* multi pci host controllers */
		uint8 function; for(function = 0; function < 8; function++) {
			if(pci_config_read_word(0, 0, function, PCI_HDR_VENDOR_ID) == 0xFFFF) /* reference said != but that's funny? */
				break;

			pci_check_bus(function);
		}
	}

	/* iterate over found devices */
	/*print_string("PCI Devices:\n");
	struct PCIDevice *device = pci_devices;
	while(device) {
		print_string("Address: ");
		print_number(device->bus);
		print_char(':');
		print_number(device->slot);
		print_char(':');
		print_number(device->function);
		print_string(" Class: ");
		print_number(device->base_class);
		print_char('.');
		print_number(device->sub_class);
		print_string(" - ");
		print_string(pci_class_to_string(device->base_class, device->sub_class));
		if(!device->driver)
			print_string(" (no driver)");
		print_char('\n');
		device = device->next;
	}*/
} 

const char *pci_class_to_string(uint8 baseclass, uint8 subclass) {
	switch(baseclass) {
		case 0x00: switch(subclass) {
				default: return "Unknown Device";
				case 0x01: return "VGA-Compatible Device";
			}
		case 0x01: switch(subclass) {
				default: return "Unknown Mass Storage Controller";
				case 0x00: return "SCSI Bus Controller";
				case 0x01: return "IDE Controller";
				case 0x02: return "Floppy Disk Controller";
				case 0x03: return "IPI Bus Controller";
				case 0x04: return "RAID Controller";
				case 0x05: return "ATA Controller";
				case 0x06: return "Serial ATA";
				case 0x07: return "Serial Attached SCSI";
			}
		case 0x02: switch(subclass) {
				default: return "Unknown Network Controller";
				case 0x00: return "Ethernet Controller";
				case 0x01: return "Token Ring Controller";
				case 0x02: return "FDDI Controller";
				case 0x03: return "ATM Controller";
				case 0x04: return "ISDN Controller";
				case 0x05: return "WorldFip Controller";
				case 0x06: return "PICMG 2.14 Multi Computing";
			}
		case 0x03: switch(subclass) {
				default: return "Unknown Display Controller";
				case 0x00: return "VGA-Compatible Controller";
				case 0x01: return "XGA Controller";
				case 0x02: return "3D Controller";
			}
		case 0x04: switch(subclass) {
				default: return "Unknown Multimedia Controller";
				case 0x00: return "Video Device";
				case 0x01: return "Audio Device";
				case 0x02: return "Computer Telephony Device";
			}
		case 0x05: switch(subclass) {
				default: return "Unknown Memory Controller";
				case 0x00: return "RAM Controller";
				case 0x01: return "Flash Controller";
			}
		case 0x06: switch(subclass) {
				default: return "Unknown Bridge Device";
				case 0x00: return "Host Bridge";
				case 0x01: return "ISA Bridge";
				case 0x02: return "EISA Bridge";
				case 0x03: return "MCA Bridge";
				case 0x04: return "PCI-to-PCI Bridge";
				case 0x05: return "PCMCIA Bridge";
				case 0x06: return "NuBus Bridge";
				case 0x07: return "CardBus Bridge";
				case 0x08: return "RACEway Bridge";
				case 0x09: return "PCI-to-PCI Bridge";
				case 0x0A: return "InfiniBrand-to-PCI Host Bridge";
			}
		case 0x07: switch(subclass) {
				default: return "Unknown Simple Communication Controller";
				case 0x00: return "Serial Controller";
				case 0x01: return "IEEE 1284 or Parallel Port";
				case 0x02: return "Multiport Serial Controller";
				case 0x03: return "Generic Modem";
				case 0x04: return "IEE 488.1/2 Controller";
				case 0x05: return "Smart Card";
			}
		case 0x08: switch(subclass) {
				default: return "Unknown Base System Peripheral";
				case 0x00: return "Interrupt Controller";
				case 0x01: return "DMA Controller";
				case 0x02: return "System Timer";
				case 0x03: return "RTC Controller";
				case 0x04: return "PCI Hot-Plug Controller";
			}
		case 0x09: switch(subclass) {
				default: return "Unknown Input Device";
				case 0x00: return "Keyboard Controller";
				case 0x01: return "Digitzer";
				case 0x02: return "Mouse Controller";
				case 0x03: return "Scanner Controller";
				case 0x04: return "Gameport Controller";
			}
		case 0x0A: switch(subclass) {
				default: return "Unknown Docking Station";
				case 0x00: return "Docking Station";
			}
		case 0x0B: switch(subclass) {
				default: return "Unknown Processor";
				/*case 0x00: return "386 Processor";
				case 0x01: return "486 Processor";
				case 0x02: return "Pentium Processor";
				case 0x03: return "Alpha Processor";
				case 0x04: return "PowerPC Processor";
				case 0x05: return "MIPS Processor";*/
				case 0x40: return "Co-Processor";
			}
		case 0x0C: switch(subclass) {
				default: return "Unknown Serial Bus Controller";
				case 0x00: return "IEE 1394 Controller";
				case 0x01: return "ACCESS.bus";
				case 0x02: return "SSA";
				case 0x03: return "USB Controller";
				case 0x04: return "Fibre Channel Controller";
				case 0x05: return "SMBus";
				case 0x06: return "InfiniBand";
				case 0x07: return "IPMI Interface";
				case 0x08: return "SERCOS Interface Standard";
				case 0x09: return "CANbus";
			}
		case 0x0D: switch(subclass) {
				default: return "Unknown Wireless Controller";
				case 0x00: return "iRDA Compatible Controller";
				case 0x01: return "Consumer IR Controller";
				case 0x10: return "RF Controller";
				case 0x11: return "Bluetooth Controller";
				case 0x12: return "Broadband Controller";
				case 0x20: return "Ethernet Controller (802.11a)";
				case 0x21: return "Ethernet Controller (802.11b)";
			}
		case 0x0E: switch(subclass) {
				default: return "Unknown Intelligent I/O Controller";
			}
		case 0x0F: switch(subclass) {
				default: return "Unknown Satellite Communication Controller";
				case 0x01: return "TV Controller";
				case 0x02: return "Audio Controller";
				case 0x03: return "Voice Controller";
				case 0x04: return "Data Controller";
			}
		case 0x10: switch(subclass) {
				default: return "Unknown Encryption/Decryption Controller";
				case 0x00: return "Network and Computing Encryption/Decryption";
				case 0x10: return "Entertainment Encryption/Decryption";
			}
		case 0x11: switch(subclass) {
				default: return "Unknown Data Acquistion or Signal Processing Controller";
				case 0x00: return "DPIO Modules";
				case 0x01: return "Performance Counters";
				case 0x10: return "Communication Syncrhonization Plus Time and Frequency Test/Measurement";
				case 0x20: return "Management Card";
			}
		default: return "Unknown Device";
	}
}
