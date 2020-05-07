#include "callback.h"
#include "iso9660.h"
#include "isr.h"
#include "fs.h"
#include "liballoc.h"
#include "scheduler.h"
#include "storage_device.h"
#include "syscall.h"
#include "text_terminal.h"
#include "thread.h"
#include "vfs.h"
#include "virtual_allocator.h"

/* wiki.osdev.org/ISO_9660 */
#define ISO_9660_SECTOR_SIZE 2048

bool scan_for_iso9660(struct StorageDevice *storageDevice);

#define ISO_9660_REQUEST_TYPE_OPEN_FILE 0
#define ISO_9660_REQUEST_TYPE_READ_FILE 1
#define ISO_9660_REQUEST_TYPE_GET_FILE_SIZE 2
#define ISO_9660_REQUEST_TYPE_CLOSE_FILE 3
#define ISO_9660_REQUEST_TYPE_COUNT_ENTRIES_IN_DIRECTORY 4
#define ISO_9660_REQUEST_TYPE_READ_ENTRIES_IN_DIRECTORY 5

struct iso9660_request {
	struct iso9660_request *next;
	uint8 type;

	union {
		struct {
			char *path;
			size_t path_length;
			openFileCallback callback;
			void *callback_tag;
		} open_file;
		struct {
			struct File *file;
			size_t dest_buffer;
			size_t file_offset;
			size_t length;
			size_t pml4;
			readFileCallback callback;
			void *callback_tag;
		} read_file;
		/* struct {
			struct File *file;
			getFileSizeCallback callback;
			void *callback_tag;
		} get_file_size; */
		struct {
			struct File *file;
			closeFileCallback callback;
			void *callback_tag;
		} close_file;
		struct {
			char *path;
			size_t path_length;
			size_t entries_offset;
			countEntriesInDirectoryCallback callback;
			void *callback_tag;
		} count_entries_in_directory;
		struct {
			char *path;
			size_t path_length;
			struct DirectoryEntry *dest_buffer;
			size_t dest_buffer_size;
			size_t pml4;
			size_t entries_offset;
			readEntriesInDirectoryCallback callback;
			void *callback_tag;
		} read_entries_in_directory;
	};
};

void init_iso9660() {
	/* add this file system */
	struct FileSystem *fs = malloc(sizeof(struct FileSystem));
	if(!fs) return;
	fs->name = "ISO 9660";
	fs->scan_handler = scan_for_iso9660;

	add_fs(fs);
}

/* queues a request */
void iso9660_queue_request(struct MountPoint *mntPt, struct iso9660_request *request) {
	request->next = 0;

	lock_interrupts();
	struct Iso9660FileSystem *fs = (struct Iso9660FileSystem *)mntPt->tag;

	if(fs->next_request)
		fs->last_request->next = request;
	else
		fs->next_request = request;

	fs->last_request = request;
	unlock_interrupts();

	/* wake up the worker thread */
	schedule_thread(fs->thread);
}


/* open the file and return the file handle, returns 0 if it couldn't be opened */
void iso9660_openFile(struct MountPoint *mntPt, char *path, size_t path_length, openFileCallback callback, void *callback_tag) {
	struct iso9660_request *request = malloc(sizeof(struct iso9660_request));
	if(!request) {
		callback(VFS_STATUS_NOMEMORY, 0, callback_tag);
		return;
	}

	request->type = ISO_9660_REQUEST_TYPE_OPEN_FILE;
	request->open_file.path = path;
	request->open_file.path_length = path_length;
	request->open_file.callback = callback;
	request->open_file.callback_tag = callback_tag;
	iso9660_queue_request(mntPt, request);
}

/* read a part of the file into the destination area of a certain address space */
void iso9660_readFile(struct MountPoint *mntPt, struct File *file, size_t dest_buffer, size_t file_offset, size_t length, size_t pml4,
	readFileCallback callback, void *callback_tag) {
	struct iso9660_request *request = malloc(sizeof(struct iso9660_request));
	if(!request) {
		callback(VFS_STATUS_NOMEMORY, callback_tag);
		return;
	}

	request->type = ISO_9660_REQUEST_TYPE_READ_FILE;
	request->read_file.file = file;
	request->read_file.dest_buffer = dest_buffer;
	request->read_file.file_offset = file_offset;
	request->read_file.length = length;
	request->read_file.pml4 = pml4;
	request->read_file.callback = callback;
	request->read_file.callback_tag = callback_tag;
	iso9660_queue_request(mntPt, request);

}

/* get the size of an opened file */
void iso9660_getFileSize(struct MountPoint *mntPt, struct File *file, getFileSizeCallback callback, void *callback_tag) {
	/*struct iso9660_request *request = malloc(sizeof(struct iso9660_request));
	if(!request) {
		callback(VFS_STATUS_NOMEMORY, 0, callback_tag);
		return;
	}

	request->type = ISO_9660_REQUEST_TYPE_GET_FILE_SIZE;
	request->get_file_size.file = file;
	request->get_file_size.callback = callback;
	request->get_file_size.callback_tag = callback_tag;
	iso9660_queue_request(mntPt, request);*/
	struct Iso9660File *f = file->tag;
	callback(VFS_STATUS_SUCCESS, f->length, callback_tag);
}

/* close the file and release the file handle */
void iso9660_closeFile(struct MountPoint *mntPt, struct File *file, closeFileCallback callback, void *callback_tag) {
	struct iso9660_request *request = malloc(sizeof(struct iso9660_request));
	if(!request) {
		callback(VFS_STATUS_NOMEMORY,  callback_tag);
		return;
	}

	request->type = ISO_9660_REQUEST_TYPE_CLOSE_FILE;
	request->close_file.file = file;
	request->close_file.callback = callback;
	request->close_file.callback_tag = callback_tag;
	iso9660_queue_request(mntPt, request);
}

/* close the mount point */
bool iso9660_unmount(struct MountPoint *mntPt) {
	/* assumes interrupts are unlocked */
	struct Iso9660FileSystem *fs = (struct Iso9660FileSystem *)mntPt->tag;

	/* is something queued? */
	if(fs->next_request)
		return false;

	/* todo, are any files open? */

	/* todo, is it currently processing something? */

	unschedule_thread(fs->thread);
	destroy_thread(fs->thread);
	free(fs->root_directory);
	free(fs->sector_buffer); 
	free(fs);

	return false;
}

/* count the entries in a directory */
void iso9660_countEntriesInDirectory(struct MountPoint *mntPt, char *path, size_t path_length, size_t entries_offset,
	countEntriesInDirectoryCallback callback, void *callback_tag) {
	struct iso9660_request *request = malloc(sizeof(struct iso9660_request));
	if(!request) {
		callback(VFS_STATUS_NOMEMORY, 0, callback_tag);
		return;
	}

	request->type = ISO_9660_REQUEST_TYPE_COUNT_ENTRIES_IN_DIRECTORY;
	request->count_entries_in_directory.path = path;
	request->count_entries_in_directory.path_length = path_length;
	request->count_entries_in_directory.entries_offset = entries_offset;
	request->count_entries_in_directory.callback = callback;
	request->count_entries_in_directory.callback_tag = callback_tag;
	iso9660_queue_request(mntPt, request);

}

/* read entries in a directory */
void iso9660_readEntriesInDirectory(struct MountPoint *mntPt, char *path, size_t path_length, struct DirectoryEntry *dest_buffer,
	size_t dest_buffer_size, size_t pml4, size_t entries_offset, readEntriesInDirectoryCallback callback, void *callback_tag) {
	struct iso9660_request *request = malloc(sizeof(struct iso9660_request));
	if(!request) {
		callback(VFS_STATUS_NOMEMORY, 0, callback_tag);
		return;
	}

	request->type = ISO_9660_REQUEST_TYPE_READ_ENTRIES_IN_DIRECTORY;
	request->read_entries_in_directory.path = path;
	request->read_entries_in_directory.path_length = path_length;
	request->read_entries_in_directory.dest_buffer = dest_buffer;
	request->read_entries_in_directory.dest_buffer_size = dest_buffer_size;
	request->read_entries_in_directory.pml4 = pml4;
	request->read_entries_in_directory.entries_offset = entries_offset;
	request->read_entries_in_directory.callback = callback;
	request->read_entries_in_directory.callback_tag = callback_tag;
	iso9660_queue_request(mntPt, request);
}

/* worker thread that does the processing */
void iso9660_thread_entry(void *tag) {
	struct MountPoint *mount_point = tag;
	struct Iso9660FileSystem *fs = mount_point->tag;
	struct StorageDevice *storageDevice = fs->device;

	/* enter the event loop */
	while(true) {
		sleep_if_not_set((size_t *)&fs->next_request);
		/* grab the top value */
		lock_interrupts();

		if(!fs->next_request) {
			/* something else woke us up */
			unlock_interrupts();
			continue;
		}

		/* take off the front element from the queue */
		struct iso9660_request *request = fs->next_request;
		if(request == fs->last_request) {
			/* clear the queue */
			fs->next_request = 0;
			fs->last_request = 0;
		} else
			fs->next_request = request->next;

		unlock_interrupts();

		switch(request->type) {
			case ISO_9660_REQUEST_TYPE_READ_FILE: {
				struct File *file = request->read_file.file;
				struct Iso9660File *f = file->tag;
				
				size_t offset = request->read_file.file_offset;

				size_t length = request->read_file.length;
				if(offset >= f->length) {
					/* past the end of the file, no length */
					request->read_file.callback(VFS_STATUS_SUCCESS, request->read_file.callback_tag);
				} else {
					if(length + offset > f->length)
						/* read past the end of the file, shorten it */
						length = f->length - offset;

					offset += (f->lba_start * fs->logical_block_size);

					struct CallbackSyncTag tag;
					tag.thread = running_thread;
					tag.response = 0;

					storageDevice->read_handler(storageDevice->tag, offset, length,
						request->read_file.pml4, (void *)request->read_file.dest_buffer, callback_sync_handler, &tag);
					while(!tag.response) sleep_if_not_set(&tag.response);

					if(tag.status != 0) {
						request->read_file.callback(VFS_STATUS_NOFILE, request->read_file.callback_tag);
					} else {
						request->read_file.callback(VFS_STATUS_SUCCESS, request->read_file.callback_tag);
					}
				}
				
				break; }
			case ISO_9660_REQUEST_TYPE_CLOSE_FILE: {
				struct File *file = request->close_file.file;
				struct Iso9660File *f = file->tag;
				free(f);
				free(file);
				request->close_file.callback(VFS_STATUS_SUCCESS, request->close_file.callback_tag);
				break; }
			case ISO_9660_REQUEST_TYPE_OPEN_FILE:
			case ISO_9660_REQUEST_TYPE_COUNT_ENTRIES_IN_DIRECTORY:
			case ISO_9660_REQUEST_TYPE_READ_ENTRIES_IN_DIRECTORY: {
				bool quit = false;

				/* scan directories */
				char *path; size_t path_length;
				switch(request->type) {
					case ISO_9660_REQUEST_TYPE_OPEN_FILE:
						path = request->open_file.path;
						path_length = request->open_file.path_length;
						break;
					case ISO_9660_REQUEST_TYPE_COUNT_ENTRIES_IN_DIRECTORY:
						path = request->count_entries_in_directory.path;
						path_length = request->count_entries_in_directory.path_length;
						break;
					case ISO_9660_REQUEST_TYPE_READ_ENTRIES_IN_DIRECTORY:
						path = request->read_entries_in_directory.path;
						path_length = request->read_entries_in_directory.path_length;

						/* enter this PML4 */
						if(request->read_entries_in_directory.pml4 != kernel_pml4)
							switch_to_address_space(request->read_entries_in_directory.pml4);

						if(request->read_entries_in_directory.dest_buffer_size < sizeof(struct DirectoryEntry)) {
							request->read_entries_in_directory.callback(VFS_STATUS_SUCCESS,
								request->read_entries_in_directory.entries_offset,
								request->read_entries_in_directory.callback_tag);
							quit = true;
						}
						break;
				}

				size_t directory_lba = (size_t)*(uint32 *)&fs->root_directory[2];
				size_t directory_length = (size_t)*(uint32 *)&fs->root_directory[10];

				/* walk our way up through the each directory */
				while(!quit) {
					/* find the next slash */
					size_t next_slash = 0;
					for(next_slash = 1; next_slash < path_length; next_slash++)
						if(path[next_slash] == '/')
							break;

					//print_fixed_string(path, next_slash);
					//print_string("\n");

					bool expectingDirectory = next_slash != path_length || request->type != ISO_9660_REQUEST_TYPE_OPEN_FILE;

					if(path_length == 0 && request->type == ISO_9660_REQUEST_TYPE_OPEN_FILE) {
						//print_string("Found our file!\n");
						struct File *file = malloc(sizeof(struct File));
						if(file == 0)
							request->open_file.callback(VFS_STATUS_NOMEMORY, 0, request->open_file.callback_tag);
						else {
							struct Iso9660File *f = malloc(sizeof(struct Iso9660File));
							if(f == 0) {
								request->open_file.callback(VFS_STATUS_NOMEMORY, 0, request->open_file.callback_tag);
								free(file);
							} else {
								file->mount_point = mount_point;
								file->tag = f;
								f->lba_start = directory_lba;
								f->length = directory_length;

								request->open_file.callback(VFS_STATUS_SUCCESS, file, request->open_file.callback_tag);
							}
						}
						
						quit = true;
					} else if((path_length == 0 &&expectingDirectory) || next_slash > 1) { /* skip over double slash, if not the last entry*/
						/*print_string("Looking for ");
						print_fixed_string(path, next_slash);
						print_string(" ");*/

						/*print_string("logical block size: ");
						print_number(fs->logical_block_size);

						print_string("\nDirectory LBA: ");
						print_number(directory_lba);*/

						//print_string("\nDirectory start: ");
						//print_number(directory_start);
						size_t offset = 0;

						/*print_string("In directory:\n");*/

						bool found = false;

						while(directory_length > 0 && !quit && !found) {
							if(offset == 0 || offset + 32 > fs->logical_block_size) {
								/* read in sector */
								offset = 0;
								size_t directory_start = directory_lba * fs->logical_block_size;
						
								struct CallbackSyncTag tag;
								tag.thread = running_thread;
								tag.response = 0;

								storageDevice->read_handler(storageDevice->tag, directory_start,
									fs->logical_block_size,
									kernel_pml4, fs->sector_buffer, callback_sync_handler, &tag);
								while(!tag.response) sleep_if_not_set(&tag.response);

								if(tag.status != 0) {
									quit = true;
									break; /* something bad happened during the read (off the disk?) */
								}

								directory_lba++; /* increment it for the next read */
							}

							/* read this record */
							size_t record_length = (size_t)(uint8)fs->sector_buffer[offset] +
								(size_t)(uint8)fs->sector_buffer[offset + 1];
							if(record_length <= 0) { /* end of the sector */
								/* jump to the next sector */
								size_t remaining_in_sector = fs->logical_block_size - offset;
								if(remaining_in_sector >= directory_length)
									directory_length = 0;
								else
									directory_length -= remaining_in_sector;


								offset = fs->logical_block_size;
								continue;
							}


							/* get the entry name */
							size_t entry_length = (size_t)(uint8)fs->sector_buffer[offset + 32];
							char *entry_name = &fs->sector_buffer[offset + 33];

							/* see if there is a Rock Ridge name to use instead */
							size_t susp_start = entry_length + 33;
							if(susp_start % 2 == 1)
								susp_start++; /* entry name is rounded up to 2 bytes */

							bool alternative_name = false;

							/* check the system user area */
							while(susp_start + 5 < record_length) {
								char sig1 = fs->sector_buffer[offset + susp_start];
								char sig2 = fs->sector_buffer[offset + susp_start + 1];
								char len = fs->sector_buffer[offset + susp_start + 2];

								if(sig1 == 'N' && sig2 == 'M') {
									/* found an alternative name */
									alternative_name = true;

									entry_length = (size_t)len - 4;
									entry_name = &fs->sector_buffer[offset + susp_start + 4];
								}

								susp_start += (size_t)len;
							}

							if(alternative_name) {

							} else {
								/* no, just our normal name, take off ; */
								size_t semi_colon;
								for(semi_colon = 0; semi_colon < entry_length; semi_colon++)
									if(entry_name[semi_colon] == ';')
										break;
								entry_length = semi_colon;
							}

							/* for some reasons, the names always start with a space */
							entry_name++;
							entry_length--;

							/* print out our name if not hidden */
							//print_fixed_string(entry_name, entry_length);//record_length - 33);
							//print_string("\n");

							/* is this what we want? */
							bool is_directory = (fs->sector_buffer[offset + 25] & (1 << 1)) == 2;
							
							if(path_length == 0) { //
								if(entry_length > 0) {
									if(request->type == ISO_9660_REQUEST_TYPE_COUNT_ENTRIES_IN_DIRECTORY) {
										/* just counting! */
										request->count_entries_in_directory.entries_offset++;
									} else {
										/* add this! */
										struct DirectoryEntry *dest_buffer =
											request->read_entries_in_directory.dest_buffer;

										dest_buffer->name_length = entry_length;
										memcpy(dest_buffer->name, entry_name,
											dest_buffer->name_length);

										if(is_directory) {
											dest_buffer->type = DIRECTORYENTRY_TYPE_DIRECTORY;
											dest_buffer->size = 0;
										} else {
											dest_buffer->type = DIRECTORYENTRY_TYPE_FILE;
											dest_buffer->size =
												(size_t)*(uint32 *)&fs->sector_buffer[offset + 10];
										}

										request->read_entries_in_directory.dest_buffer++;
										request->read_entries_in_directory.dest_buffer_size -=
											sizeof(struct DirectoryEntry);
										request->read_entries_in_directory.entries_offset++;


										if(request->read_entries_in_directory.dest_buffer_size <
											sizeof(struct DirectoryEntry))
											quit = true;
									}
								}
								
							} else if(is_directory == expectingDirectory && next_slash - 1 == entry_length
								&& strcmp(entry_name, path + 1, entry_length) == 0) {
								found = true;
								directory_lba = (size_t)*(uint32 *)&fs->sector_buffer[offset + 2];
								directory_length = (size_t)*(uint32 *)&fs->sector_buffer[offset + 10];
					//			print_string(" Found!");
							}

					//		print_string("\n");

							if(!found) {
								/* jump to the next record */
								directory_length -= record_length;
								offset += record_length;
							}
						}

						if(path_length == 0) {
							/* finished iterating over the directory */
							switch(request->type) {
								case ISO_9660_REQUEST_TYPE_COUNT_ENTRIES_IN_DIRECTORY:
									request->count_entries_in_directory.callback(VFS_STATUS_SUCCESS,
										request->count_entries_in_directory.entries_offset,
										request->count_entries_in_directory.callback_tag);
									
									break;
								case ISO_9660_REQUEST_TYPE_READ_ENTRIES_IN_DIRECTORY:
									request->read_entries_in_directory.callback(VFS_STATUS_SUCCESS,
										request->read_entries_in_directory.entries_offset,
										request->read_entries_in_directory.callback_tag);
									break;
							}
							quit = true;
						} else if(!found) {
							quit = true;
							/* print_string("Can't find in directory entry!\n"); */

							switch(request->type) {
							case ISO_9660_REQUEST_TYPE_OPEN_FILE:
								request->open_file.callback(VFS_STATUS_NOFILE, 0, request->open_file.callback_tag);
								break;
							case ISO_9660_REQUEST_TYPE_COUNT_ENTRIES_IN_DIRECTORY:
								request->count_entries_in_directory.callback(VFS_STATUS_NOFILE, 0,
									request->count_entries_in_directory.callback_tag);
								break;
							case ISO_9660_REQUEST_TYPE_READ_ENTRIES_IN_DIRECTORY:
								request->read_entries_in_directory.callback(VFS_STATUS_NOFILE, 0,
									request->read_entries_in_directory.callback_tag);
								break;
							}
						}
					}

					path += next_slash;
					path_length -= next_slash;
				}
			break;  }
		}

		free(request);
	}
};

bool scan_for_iso9660(struct StorageDevice *storageDevice) {
	/* buffer for reading data into */
	char *buffer = malloc(ISO_9660_SECTOR_SIZE);
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
		struct CallbackSyncTag tag;
		tag.thread = running_thread;
		tag.response = 0;

		storageDevice->read_handler(storageDevice->tag, sector * ISO_9660_SECTOR_SIZE, ISO_9660_SECTOR_SIZE,
			kernel_pml4, buffer, callback_sync_handler, &tag);
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

	struct Iso9660FileSystem *fs = malloc(sizeof(struct Iso9660FileSystem));
	if(!fs) { free(buffer); return false; }

	fs->volume_blocks = *(uint32 *)&buffer[80];
	fs->logical_block_size = *(uint16 *)&buffer[128];
	fs->device = storageDevice;

	/* temp buffer */
	fs->sector_buffer = malloc(fs->logical_block_size);
	if(!fs->sector_buffer) { free(fs); free(buffer); return false; }

	/* copy root directory entry */
	fs->root_directory = malloc(34);
	if(!fs->root_directory) { free(fs->sector_buffer); free(fs); free(buffer); return false; }
	memcpy(fs->root_directory, &buffer[156], 34);
	free(buffer);

	/* mount this */
	struct MountPoint *mount_point = malloc(sizeof(struct MountPoint));
	if(!mount_point) { free(fs->root_directory); free(fs->sector_buffer); free(fs); return false; }

	fs->next_request = 0;
	fs->last_request = 0;

	fs->thread = create_thread(0, (size_t)iso9660_thread_entry, (size_t)mount_point);
	if(!fs->thread) { free(mount_point); free(fs->root_directory); free(fs->sector_buffer); free(fs); return false; }

	mount_point->tag = fs;

	mount_point->fs_name = "ISO 9660";

	mount_point->open_file_handler = iso9660_openFile;
	mount_point->get_file_size_handler = iso9660_getFileSize;
	mount_point->read_file_handler = iso9660_readFile;
	mount_point->close_file_handler = iso9660_closeFile;
	mount_point->unmount_handler = iso9660_unmount;
	mount_point->count_entries_in_directory_handler = iso9660_countEntriesInDirectory;
	mount_point->read_entries_in_directory_handler = iso9660_readEntriesInDirectory;

	mount_point->storage_device = storageDevice;

	
	/* attempt to mount this from /cd1/ to /cd999/ */
	size_t i; for(i = 1; i < 1000; i++) {
		/* there needs to be a cleaner way to do this, I'm worn out right now */
		if(i == 1) {
			mount_point->path = malloc(5);
			if(!mount_point->path) { free(mount_point); free(fs->thread); free(fs->root_directory); free(fs->sector_buffer);  free(fs); return false; }
			memcpy(mount_point->path, "/cd1/", 5);
			mount_point->path_length = 5;
		} else if(i == 10) {
			free(mount_point->path);
			mount_point->path = malloc(6);
			if(!mount_point->path) { free(mount_point); free(fs->thread); free(fs->root_directory); free(fs->sector_buffer);  free(fs); return false; }
			memcpy(mount_point->path, "/cd10/", 6);
			mount_point->path_length = 6;

		} else if(i == 100) {
			free(mount_point->path);
			mount_point->path = malloc(7);
			if(!mount_point->path) { free(mount_point); free(fs->thread); free(fs->root_directory); free(fs->sector_buffer);  free(fs); return false; }
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
	free(fs->thread);
	free(mount_point->path);
	free(mount_point); free(fs->root_directory); free(fs->sector_buffer); free(fs);
	return false;
}
