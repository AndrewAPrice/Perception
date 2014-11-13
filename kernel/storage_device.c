#include "isr.h"
#include "fs.h"
#include "storage_device.h"
#include "text_terminal.h"

struct StorageDevice *first_storage_device;

void init_storage_devices() {
	first_storage_device = 0;
};

void add_storage_device(struct StorageDevice *storage_device) {
	lock_interrupts();
	storage_device->next = first_storage_device;
	storage_device->previous = 0;
	if(first_storage_device)
		first_storage_device->previous = storage_device;

	first_storage_device = storage_device;
	unlock_interrupts();

	if(storage_device->inserted)
		scan_for_fs(storage_device);

}

/* prints a size in a nice format with units */
void print_size(size_t size) {
	if(size == 0) /* handle special case, otherwise it wouldn't print anything at all */
		print_string(" 0 B");
	else {
		char *units[] = {"EB", "PB", "TB", "GB", "MB", "KB", "B"};
		size_t sizes[] = {
			(size >> 60) & 1023,
			(size >> 50) & 1023,
			(size >> 40) & 1023,
			(size >> 30) & 1023,
			(size >> 20) & 1023,
			(size >> 10) & 1023,
			size & 1023
		};

		int i; for(i = 0; i < 7; i++) {
			/* has a value at this unit? */
			if(sizes[i]) {
				print_char(' ');
				print_number(sizes[i]);
				print_char(' ');
				print_string(units[i]);
			}
		}
	}
}
