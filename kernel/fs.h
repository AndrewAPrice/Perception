#pragma once
#include "types.h"


struct StorageDevice;

typedef bool (*scanForFileSystem)(struct StorageDevice *storage_device);


struct FileSystem {
	scanForFileSystem scan_handler;
	char *name;
	struct FileSystem *next;
};

extern void init_fs();

/* adds a file system */
extern void add_fs(struct FileSystem *file_system);

/* scans a storage device for a filesystem and mounts it */
extern void scan_for_fs(struct StorageDevice *storage_device);