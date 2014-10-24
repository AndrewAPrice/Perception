#include "storage_device.h"

struct StorageDevice *first_storage_device;

void init_storage_devices() {
	first_storage_device = 0;
};

void add_storage_device(struct StorageDevice *storage_device) {
	storage_device->next = first_storage_device;
	storage_device->previous = 0;
	if(first_storage_device)
		first_storage_device->previous = storage_device;

	first_storage_device = storage_device;
}