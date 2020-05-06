#include "io.h"
#include "isr.h"
#include "shell.h"
#include "storage_device.h"
#include "text_terminal.h"
#include "vfs.h"

struct MountPoint *firstMountPoint;
void init_vfs() {
	firstMountPoint = 0;
}

bool mount(struct MountPoint *mount_point) {
	/* see if there's a bad name */
	if(mount_point->path_length == 0 || mount_point->path[0] != '/' || mount_point->path[mount_point->path_length - 1] != '/')
		return false;

	lock_interrupts();
	/* scan mount points to see if a conflicting name exists */
	struct MountPoint *mp = firstMountPoint;
	while(mp != 0) {
		/* compare name */
		if(mp->path_length == mount_point->path_length &&
			strcmp(mp->path, mount_point->path, mp->path_length) == 0)
			return false; /* conflicting name */

		mp = mp->next;
	}

	/* add this mount point */
	mount_point->next = firstMountPoint;
	firstMountPoint = mount_point;

	/* figure out the parent path */
	/* find 2nd to last slash */
	size_t p; for(p = mount_point->path_length - 2; true; p--) {
		if(mount_point->path[p] == '/') {
			mount_point->parent_path_length = p + 1;
			break;
		}
	}
	unlock_interrupts();
#if 1
	print_string("Mounted ");
	print_fixed_string(mount_point->path, mount_point->path_length);
	print_string(" - ");
	print_string(mount_point->fs_name);
	print_string(" -");
	print_size(mount_point->storage_device->size);
	print_char('\n');
#endif
	shell_disk_mounted();
	return true;
}

bool unmount(char *mount_point_path, size_t path_length) {
	lock_interrupts();
	/* scan each mount point */
	struct MountPoint *previousMountPoint = 0;
	struct MountPoint *mountPoint = firstMountPoint;
	while(mountPoint != 0) {
		if(mountPoint->path_length == path_length && strcmp(mount_point_path, mountPoint->path, path_length) == 0) {
			/* this is the mountpoint we want to unmount */
			bool unmount = mountPoint->unmount_handler(mountPoint);

			if(unmount) {			
				/* remove form the linked list chain */
				if(previousMountPoint)
					previousMountPoint->next = mountPoint->next;
				else
					firstMountPoint = mountPoint->next;
				
				/* release the memory */
				free(mountPoint->path);
				free(mountPoint);
			}

			unlock_interrupts();
			return unmount;
			
		}
		/* go to the next mount point */
		previousMountPoint = mountPoint;
		mountPoint = mountPoint->next;
	}
	unlock_interrupts();
	return false;
}

struct MountPoint *find_mount_point(char *path, size_t path_length) {
	if(path_length == 0)
		return 0;

	/* find the mount point */
	struct MountPoint *bestMountPoint = 0;
	size_t bestMountPointLength = 0;

	/* loop through each mount point */
	lock_interrupts();
	struct MountPoint *currentMountPoint = firstMountPoint;
	while(currentMountPoint != 0) {
		if(path_length >= currentMountPoint->path_length &&
			currentMountPoint->path_length > bestMountPointLength &&
			strcmp(path, currentMountPoint->path, currentMountPoint->path_length) == 0) {
			bestMountPoint = currentMountPoint;
			bestMountPointLength = currentMountPoint->path_length;
		}

		/* go to the next mount point */
		currentMountPoint = currentMountPoint->next;
	}
	unlock_interrupts();

	return bestMountPoint;
}

void open_file(char *path, size_t path_length, openFileCallback callback, void *tag) {
	/* find the mount point */
	struct MountPoint *mountPoint = find_mount_point(path, path_length);
	if(mountPoint == 0) {
		/* couldn't find any mount points */
		callback(VFS_STATUS_NOFILE, 0, tag);
		return;
	}

	mountPoint->open_file_handler(mountPoint, (char *)((size_t)path + (mountPoint->path_length - 1)), path_length - mountPoint->path_length + 1,
		callback, tag);
}

void close_file(struct File *file, closeFileCallback callback, void *tag) {
	if(file == 0) {
		callback(VFS_STATUS_NOFILE, tag);
		return;
	}

	/* close the file */
	file->mount_point->close_file_handler(file->mount_point, file, callback, tag);
}

void get_file_size(struct File *file, getFileSizeCallback callback, void *tag) {
	if(file == 0) {
		callback(VFS_STATUS_NOFILE, 0, tag);
		return;
	}

	file->mount_point->get_file_size_handler(file->mount_point, file, callback, tag);
}

void read_file(struct File *file, size_t dest_buffer, size_t file_offset, size_t length, size_t pml4, readFileCallback callback, void *tag) {
	/* find the mount point */
	if(file == 0) {
		callback(VFS_STATUS_NOFILE, tag);
		return;
	}


	file->mount_point->read_file_handler(file->mount_point, file, dest_buffer, file_offset, length, pml4, callback, tag);
}

void count_entries_in_directory(char *path, size_t path_length, countEntriesInDirectoryCallback callback, void *tag) {
	if(path_length == 0 || path[0] != '/' || path[path_length - 1] != '/') {
		callback(VFS_STATUS_BADNAME, 0, tag); /* invalid path */
		return;
	}
	
	size_t entries = 0;

	/* loop through each mount point, to see if any mount points are in this path */
	struct MountPoint *currentMountPoint = firstMountPoint;
	while(currentMountPoint != 0) {
		if(currentMountPoint->parent_path_length == path_length
			&& strcmp(path, currentMountPoint->path, currentMountPoint->parent_path_length) != 0) {
			entries++;
		}

		/* go to the next mount point */
		currentMountPoint = currentMountPoint->next;
	}

	/* find the mount point */
	struct MountPoint *mountPoint = find_mount_point(path, path_length);
	if(mountPoint == 0) {
		print_string("e");
		/* couldn't find any mount points */
		callback(VFS_STATUS_SUCCESS, entries, tag);
		return;
	}

	mountPoint->count_entries_in_directory_handler(mountPoint, (char *)((size_t)path + (mountPoint->path_length - 1)),
		path_length - mountPoint->path_length + 1, entries, callback, tag);
}

void read_entries_in_directory(char *path, size_t path_length, struct DirectoryEntry *dest_buffer, size_t dest_buffer_size, size_t pml4,
	readEntriesInDirectoryCallback callback, void *tag) {
	if(path_length == 0 || path[0] != '/' || path[path_length - 1] != '/') {
		callback(VFS_STATUS_BADNAME, 0, tag); /* invalid path */
		return;
	}

	size_t entries = 0;

	/* loop through each mount point, to see if any mount points are in this path */
	struct MountPoint *currentMountPoint = firstMountPoint;
	while(currentMountPoint != 0) {
		if(currentMountPoint->parent_path_length == path_length
			&& strcmp(path, currentMountPoint->path, currentMountPoint->parent_path_length) != 0) {
			/* found an entry, */
			
			/* check if there's room to write this entry */
			switch_to_address_space(pml4);
			if(dest_buffer_size >= sizeof(struct DirectoryEntry)) {
				/* write out a directory entry for this mount point */
				dest_buffer->name_length = currentMountPoint->path_length - currentMountPoint->parent_path_length - 1;
				memcpy(dest_buffer->name, &currentMountPoint->path[currentMountPoint->path_length], dest_buffer->name_length);

				dest_buffer->type = DIRECTORYENTRY_TYPE_MOUNTPOINT;
				dest_buffer->size = 0;

				dest_buffer++;
				dest_buffer_size -= sizeof(struct DirectoryEntry);
				entries++;
			}
		}

		/* go to the next mount point */
		currentMountPoint = currentMountPoint->next;
	}

	/* find the mount point */
	struct MountPoint *mountPoint = find_mount_point(path, path_length);
	if(mountPoint == 0) {
		/* couldn't find any mount points */
		callback(VFS_STATUS_SUCCESS, entries, tag);
		return;
	}

	mountPoint->read_entries_in_directory_handler(mountPoint, (char *)((size_t)path + (mountPoint->path_length - 1)),
		path_length - mountPoint->path_length + 1, dest_buffer, dest_buffer_size, pml4, entries, callback, tag);
}
