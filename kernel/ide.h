#pragma once
#include "pci.h"

struct IDEChannelRegisters {
	uint16 io_base; /* I/O base */
	uint16 control_base; /* control base */
	uint16 bus_master_id; /* bus master ide */
	unsigned char no_interrupt; /* no interrupt */
};

struct IDEController;
struct Thread;
struct StorageDevice;

struct IDEDevice {
	uint8 Reserved; /* 0 - empty, 1 - exists */
	uint8 Channel; /* 0 - primary, 1 - secondary */
	uint8 Drive; /* 0 - master, 1 - slave */
	uint16 Type;
	uint16 Signature;
	uint16 Capabilities;
	uint32 CommandSets; /* supported command sets */
	uint32 Size; /* size in sectors */
	unsigned char Model[41]; /* model string */
	struct IDEController * Controller;
	struct IDEDevice *Next;
	struct StorageDevice *storage_device;
};


struct IDEController {
	struct IDEChannelRegisters Channels[2];
	struct IDEDevice *Devices;
	struct Thread *thread; /* the thread that handles this */
};

extern void init_ide(struct PCIDevice *device);
