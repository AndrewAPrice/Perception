#pragma once
#include "types.h"

struct StorageDevice;

struct Iso9960FileSystem {
	struct StorageDevice *device;
	uint32 volume_blocks; /* size of the volume in logical blocks */
	uint16 logical_block_size; /* logical block size in bytes, could be something other than 2 KB */
	char *root_directory;
};

extern void init_iso9660();
