#pragma once
#include "types.h"

struct File;
struct MountPoint;

/* open the file and return the file handle, returns 0 if it couldn't be opened */
typedef struct File *(*openFilePtr)(struct MountPoint *mntPt, char *path, size_t path_length);
/* read a part of the file into the destination area of a certain address space */
typedef void (*readFilePtr)(struct MountPoint *mntPt, struct File *file, size_t dest_buffer, size_t file_offset, size_t length, size_t pml4);
/* get the size of an opened file */
typedef size_t (*getFileSizePtr)(struct MountPoint *mntPt, struct File *file);
/* close the file and release the file handle */
typedef void (*closeFilePtr)(struct MountPoint *mntPt, struct File *file);
/* close the mount point */
typedef void (*unmountPtr)(struct MountPoint *mntPt);
/* count the entries in a directory */
typedef size_t (*countEntriesInDirectoryPtr)(struct MountPoint *mntPt, char *path, size_t path_length);
/* read entries in a directory */
typedef void (*readEntriesInDirectoryPtr)(struct MountPoint *mntPt, char *path, size_t path_length, size_t dest_buffer, size_t dest_buffer_size, size_t pml4);

struct StorageDevice;

struct MountPoint {
	char *path; /* must start and end with a /, dynamically allocated */
	unsigned short path_length;
	const char *fs_name; /* filesystem name */
	void *tag;
	openFilePtr open_file_handler;
	getFileSizePtr get_file_size_handler;
	readFilePtr read_file_handler;
	closeFilePtr close_file_handler;
	unmountPtr unmount_handler;
	countEntriesInDirectoryPtr count_entries_in_directory_handler;
	readEntriesInDirectoryPtr read_entries_in_directory_handler;

	struct StorageDevice *storage_device;

	struct MountPoint *next;
};

struct File {
	struct MountPoint *mount_point;
	struct File *next;
};


extern void init_vfs();
extern bool mount(struct MountPoint *mount_point);
extern void unmount(char *mount_point_path, size_t path_length);

extern struct MountPoint *find_mount_point(char *path, size_t path_length);
extern struct File *open_file(char *path, size_t path_length);
extern void close_file(struct File *file);
extern size_t get_file_size(struct File *file);
extern void read_file(struct File *file, size_t dest_buffer, size_t file_offset, size_t length, size_t pml4);
extern int count_entries_in_directory(char *path, size_t path_length);
extern void read_entries_in_directory(char *path, size_t path_length, size_t dest_buffer, size_t dest_buffer_size, size_t pml4);
