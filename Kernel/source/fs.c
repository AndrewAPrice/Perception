#include "fs.h"
#include "iso9660.h"
#include "scheduler.h"
#include "storage_device.h"
#include "syscall.h"
#include "thread.h"

/* linked list of file systems */
struct FileSystem *file_systems;

void init_fs() {
	file_systems = 0;

	/* add file systems here */
	init_iso9660();
}

void add_fs(struct FileSystem *file_system) {
	file_system->next = file_systems;
	file_systems = file_system;
}

void scan_for_fs_entry(void *tag) {
	/* worker thread - scan for file systems on this device */
	struct StorageDevice *storage_device = (struct StorageDevice *)tag;

	/* loop over each file system and find one for this device,  */
	struct FileSystem *fs = file_systems;
	bool success = false;
	while(!success && fs != 0) {
		success = fs->scan_handler(storage_device);
		fs = fs->next;
	}

	#if 1
	if(!success) {
		/* print out the device that was added */
		print_string("Couldn't mount ");

		switch(storage_device->type) {
			case STORAGE_DEVICE_TYPE_UNKNOWN: default: print_string("Unknown Drive"); break;
			case STORAGE_DEVICE_TYPE_OPTICAL: print_string("Optical Drive"); break;
			case STORAGE_DEVICE_TYPE_FLOPPY: print_string("Floppy Drive"); break;
			case STORAGE_DEVICE_TYPE_HARDDRIVE: print_string("Hard Drive"); break;
			case STORAGE_DEVICE_TYPE_FLASH: print_string("Flash Drive"); break;
		}

		if(!storage_device->inserted)
			print_string(" - Not Inserted");
	
		if(storage_device->size > 0) {
			print_string(" -");
			print_size(storage_device->size);
		}
		print_char('\n');
	}
	#endif

	terminate_thread();
}

/* detects a filesystem on a storage device and mounts it */
void scan_for_fs(struct StorageDevice *storage_device) {
	/* create a thread to scan for file systems on this device */
	struct Thread *thread = create_thread(0, (size_t)scan_for_fs_entry, (size_t)storage_device);
	if(!thread)
		return;

	schedule_thread(thread);
}
