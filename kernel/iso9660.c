#include "iso9660.h"
#include "fs.h"
#include "liballoc.h"
#include "scheduler.h"
#include "storage_device.h"
#include "syscall.h"
#include "text_terminal.h"
#include "vfs.h"
#include "virtual_allocator.h"

/* wiki.osdev.org/ISO_9660 */
#define ISO_9660_SECTOR_SIZE 2048

bool scan_for_iso9660(struct StorageDevice *storageDevice);

void init_iso9660() {
	/* add this file system */
	struct FileSystem *fs = (struct FileSystem *)malloc(sizeof(struct FileSystem));
	if(!fs) return;
	fs->name = "ISO 9660";
	fs->scan_handler = scan_for_iso9660;

	add_fs(fs);
}

struct iso9660_callback_tag {
	size_t response;
	size_t status;
	struct Thread *thread;
};

void iso9660_callback(size_t status, void *tag) {
	struct iso9660_callback_tag *_tag = (struct iso9660_callback_tag *)tag;
	_tag->status = status;
	
	/* wake this thread */
	_tag->response = 1;
	schedule_thread(_tag->thread);
}


bool scan_for_iso9660(struct StorageDevice *storageDevice) {
	/* buffer for reading data into */
	char *buffer = (char *)malloc(ISO_9660_SECTOR_SIZE);
	if(!buffer) return;

	/* start at sector 0x10 and keep looping until we run out of space, stop finding volume descriptors, or
	   find the primary volume descriptor */
	size_t sector = 0x10;
	bool found_primary_volume = false;

	while(!found_primary_volume) {
		if(sector * ISO_9660_SECTOR_SIZE > storageDevice->size - ISO_9660_SECTOR_SIZE) {
			/* reached the end of the device */
			free(buffer);
			return false;
		}

		/* read in this sector */
		struct iso9660_callback_tag tag;
		tag.thread = running_thread;
		tag.response = 0;

		storageDevice->read_handler(storageDevice->tag, sector * ISO_9660_SECTOR_SIZE, ISO_9660_SECTOR_SIZE,
			kernel_pml4, buffer, iso9660_callback, &tag);
		while(!tag.response) sleep_if_not_set(&tag.response);
		
		if(/* did it error? */
			tag.status != STORAGE_DEVICE_CALLBACK_STATUS_SUCCESS ||
			/* are bytes 1-5 "CD001"? */
			buffer[1] != 'C' || buffer[2] != 'D' || buffer[3] != '0' || buffer[4] != '0' || buffer[5] != '1') {
			/* no more volume descriptors or an error */
			free(buffer);
			return false;
		}

		/* is this a primary volume descriptor? */
		if(buffer[0] == 1)
			found_primary_volume = true;
		else
			/* jump to the next sector */
			sector++;
	}

	if(buffer[6] != 0x01 || /* check version */
		*(uint16 *)&buffer[120] > 1 || /* set size - we only support single set disks */
		buffer[881] != 0x01 /* directory records and path table version */
		) {
		free(buffer);
		return false;
	}

	struct Iso9960FileSystem *fs = (struct Iso9960FileSystem *)malloc(sizeof(struct Iso9960FileSystem));
	if(!fs) { free(buffer); return false; }

	fs->volume_blocks = *(uint32 *)&buffer[80];
	fs->logical_block_size = *(uint16 *)&buffer[128];

	/* copy root directory entry */
	fs->root_directory = (char *)malloc(34);
	if(!fs->root_directory) { free(fs); free(buffer); return false; }
	memcpy(fs->root_directory, &buffer[156], 34);
	free(buffer);

	/* mount this */
	struct MountPoint *mount_point = (struct MountPoint *)malloc(sizeof(struct MountPoint));
	if(!mount_point) { free(fs->root_directory); free(fs); return false; }

	mount_point->tag = fs;

	mount_point->fs_name = "ISO 9660";

	mount_point->open_file_handler = 0;
	mount_point->get_file_size_handler = 0;
	mount_point->read_file_handler = 0;
	mount_point->close_file_handler = 0;
	mount_point->unmount_handler = 0;
	mount_point->count_entries_in_directory_handler = 0;
	mount_point->read_entries_in_directory_handler = 0;

	mount_point->storage_device = storageDevice;

	/* attempt to mount this from /cd1/ to /cd999/ */
	size_t i; for(i = 1; i < 1000; i++) {
		/* there needs to be a cleaner way to do this, I'm worn out right now */
		if(i == 1) {
			mount_point->path = (char *)malloc(5);
			if(!mount_point->path) { free(mount_point); free(fs->root_directory); free(fs); return false; }
			memcpy(mount_point->path, "/cd1/", 5);
			mount_point->path_length = 5;
		} else if(i == 10) {
			free(mount_point->path);
			mount_point->path = (char *)malloc(6);
			if(!mount_point->path) { free(mount_point); free(fs->root_directory); free(fs); return false; }
			memcpy(mount_point->path, "/cd10/", 6);
			mount_point->path_length = 6;

		} else if(i == 100) {
			free(mount_point->path);
			mount_point->path = (char *)malloc(7);
			if(!mount_point->path) { free(mount_point); free(fs->root_directory); free(fs); return false; }
			memcpy(mount_point->path, "/cd100/", 7);
			mount_point->path_length = 7;
		} else if(i < 10) {
			mount_point->path[3] = '0' + i;
		} else if(i < 100) {
			mount_point->path[3] = '0' + i / 10;
			mount_point->path[4] = '0' + i % 10;
		} else {
			mount_point->path[3] = '0' + i / 100;
			mount_point->path[4] = '0' + (i % 100) / 10;
			mount_point->path[5] = '0' + i % 10;
		}

		/* attempt to mount it with this version */
		if(mount(mount_point))
			return true;

	}

	/* couldn't mount, beyond cd999 */
	free(mount_point->path);
	free(mount_point); free(fs->root_directory); free(fs);
	return false;
}
