#pragma once
#include "types.h"

#define STORAGE_DEVICE_TYPE_UNKNOWN 0
#define STORAGE_DEVICE_TYPE_OPTICAL 1
#define STORAGE_DEVICE_TYPE_FLOPPY 2
#define STORAGE_DEVICE_TYPE_HARDDRIVE 3
#define STORAGE_DEVICE_TYPE_FLASH 4

typedef void (*StorageDeviceCallback)(size_t status, void *tag);

typedef void (*StorageDeviceRead)(void *storage_device_tag, size_t offset, size_t length, size_t pml4, size_t dest_buffer,
	StorageDeviceCallback callback, void *callback_tag);

struct StorageDevice {
	int type : 3; /* type of the medium */
	size_t size; /* size of the inserted medium */
	int inserted : 1; /* medium inserted? */

	StorageDeviceRead read_function; /* function to read */

	void *tag; /* specific stuff, used by the driver */

	/* linked list of storage devices */
	struct StorageDevice *next;
	struct StorageDevice *previous;
};

extern void init_storage_devices();
extern void add_storage_device(struct StorageDevice *storage);