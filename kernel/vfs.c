#include "vfs.h"
#include "io.h"

struct MountPoint *firstMountPoint;
void init_vfs() {
	firstMountPoint = 0;
}

void mount(struct MountPoint *mount_point) {
	mount_point->next = firstMountPoint;
	firstMountPoint = mount_point;
}

void unmount(char *mount_point_path, size_t path_length) {
	/* scan each mount point */
	struct MountPoint *previousMountPoint = 0;
	struct MountPoint *mountPoint = firstMountPoint;
	while(mountPoint != 0) {
		if(mountPoint->path_length == path_length && strcmp(mount_point_path, mountPoint->path, path_length) == 0) {
			/* this is the mountpoint we want to unmount */
			
			/* remove form the linked list chain */
			if(previousMountPoint)
				previousMountPoint->next = mountPoint->next;
			else
				firstMountPoint = mountPoint->next;

			/* release the memory */
			mountPoint->unmount_handler(mountPoint);
			return;
		}

		/* go to the next mount point */
		previousMountPoint = mountPoint;
		mountPoint = mountPoint->next;
	}
}

struct MountPoint *find_mount_point(char *path, size_t path_length) {
	if(path_length == 0)
		return 0;

	/* find the mount point */
	struct MountPoint *bestMountPoint = 0;
	size_t bestMountPointLength = 0;

	/* loop through each mount point */
	struct MountPoint *currentMountPoint = firstMountPoint;
	while(currentMountPoint != 0) {
		if(path_length >= currentMountPoint->path_length &&
			currentMountPoint->path_length > bestMountPointLength &&
			strcmp(path, currentMountPoint->path, currentMountPoint->path_length) != 0) {
			bestMountPoint = currentMountPoint;
			bestMountPointLength = 0;
		}

		/* go to the next mount point */
		currentMountPoint = currentMountPoint->next;
	}

	return bestMountPoint;
}

struct File *open_file(char *path, size_t path_length) {
	/* find the mount point */
	struct MountPoint *mountPoint = find_mount_point(path, path_length);
	if(mountPoint == 0)
		return 0; /* couldn't find any mount points */

	return mountPoint->open_file_handler(mountPoint, (char *)((size_t)path + mountPoint->path_length), path_length - mountPoint->path_length);
}

void close_file(struct File *file) {
	if(file == 0)
		return;

	/* close the file */
	file->mount_point->close_file_handler(file->mount_point, file);
}

size_t get_file_size(struct File *file) {
	if(file == 0)
		return 0;

	return file->mount_point->get_file_size_handler(file->mount_point, file);
}

void read_file(struct File *file, size_t dest_buffer, size_t file_offset, size_t length, size_t pml4) {
	/* find the mount point */
	if(file == 0)
		return;

	file->mount_point->read_file_handler(file->mount_point, file, dest_buffer, file_offset, length, pml4);
}

int count_entries_in_directory(char *path, size_t path_length) {
	/* find the mount point */
	struct MountPoint *mountPoint = find_mount_point(path, path_length);
	if(mountPoint == 0)
		return 0; /* couldn't find any mount points */

	return mountPoint->count_entries_in_directory_handler(mountPoint, path, path_length);
}

void read_entries_in_directory(char *path, size_t path_length, size_t dest_buffer, size_t dest_buffer_size, size_t pml4) {
	/* find the mount point */
	struct MountPoint *mountPoint = find_mount_point(path, path_length);
	if(mountPoint == 0)
		return; /* couldn't find any mount points */

	mountPoint->count_entries_in_directory_handler(mountPoint, path, path_length);
}
