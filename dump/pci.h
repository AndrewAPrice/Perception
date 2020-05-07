#pragma once
#include "types.h"

/* offsets into the pci header */
#define PCI_HDR_VENDOR_ID 0 /* word */
#define PCI_HDR_DEVICE_ID 2 /* word */
#define PCI_HDR_COMMAND 4 /* word */
#define PCI_HDR_STATUS 6 /* word */
#define PCI_HDR_REVISION_ID 8 /* byte */
#define PCI_HDR_PROG_IF 9 /* byte */
#define PCI_HDR_SUBCLASS 10 /* byte */
#define PCI_HDR_CLASS_CODE 11 /* byte */
#define PCI_HDR_CACHE_LINE_SIZE 12 /* byte */
#define PCI_HDR_LATENCY_TIMER 13 /* byte */
#define PCI_HDR_HEADER_TYPE 14 /* byte */
#define PCI_HDR_BIST 15 /* byte */
#define PCI_HDR_BAR0 16 /* dword */
#define PCI_HDR_BAR1 20 /* dword */
#define PCI_HDR_BAR2 24 /* dword */
#define PCI_HDR_BAR3 28 /* dword */
#define PCI_HDR_BAR4 32 /* dword */
#define PCI_HDR_BAR5 36 /* dword */

#define PCI_HDR_SECONDARY_BUS_NUMBER 25 /* byte */

extern uint32 pci_config_read_dword(uint8 bus, uint8 slot, uint8 func, uint8 offset);
extern uint16 pci_config_read_word(uint8 bus, uint8 slot, uint8 func, uint8 offset);
extern uint8 pci_config_read_byte(uint8 bus, uint8 slot, uint8 func, uint8 offset);

struct PCIDevice {
	uint8 base_class;
	uint8 sub_class;
	uint16 vendor_id;
	uint16 device_id;
	uint8 bus;
	uint8 slot;
	uint8 function;
	int driver : 1;

	struct PCIDevice *next;
};

extern struct PCIDevice *pci_devices;

extern void init_pci();
extern const char *pci_class_to_string(uint8 baseclass, uint8 subclass);