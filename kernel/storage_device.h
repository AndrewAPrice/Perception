#pragma once
#include "types.h"

#define STORAGE_DEVICE_TYPE_UNKNOWN 0
#define STORAGE_DEVICE_TYPE_OPTICAL 1

typedef void (*StorageDeviceCallback)(size_t status, void *tag);

typedef void (*StorageDeviceRead)(size_t offset, size_t length, size_t pml4, size_t dest_buffer, size_t StorageDeviceCallback callback, void *tag);

struct StorageDevice {
	uchar8 type; /* type of the medium */
	size_t size; /* size of the inserted medium */
	int inserted : 1; /* medium inserted? */

	StorageDeviceRead read_function; /* function to read */

	void *data; /* specific stuff, used by the driver */
};

