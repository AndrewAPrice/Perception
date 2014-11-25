#pragma once
#include "types.h"

struct StorageDevice;
struct iso9660_request;
struct Thread;

struct Iso9660FileSystem {
	struct StorageDevice *device;
	uint32 volume_blocks; /* size of the volume in logical blocks */
	uint16 logical_block_size; /* logical block size in bytes, could be something other than 2 KB */
	char *root_directory;
	struct Thread *thread;
	char *sector_buffer; /* temp buffer to read sectors into */

	/* a queue of requests */
	struct iso9660_request *next_request;
	struct iso9660_request *last_request;
};

struct Iso9660File {
	uint32 lba_start;
	uint32 length;
};

extern void init_iso9660();
