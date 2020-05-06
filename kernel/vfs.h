#pragma once
#include "types.h"

#define VFS_STATUS_SUCCESS 0
#define VFS_STATUS_BADNAME 1
#define VFS_STATUS_NOFILE 2
#define VFS_STATUS_NOMEMORY 3

struct File;
struct MountPoint;
struct DirectoryEntry;

/* open the file and return the file handle, returns 0 if it couldn't be opened */
typedef void (*openFileCallback)(size_t status, struct File *file, void *tag);
typedef void (*openFilePtr)(struct MountPoint *mntPt, char *path, size_t path_length, openFileCallback callback, void *callback_tag);

/* read a part of the file into the destination area of a certain address space */
typedef void (*readFileCallback)(size_t status, void *tag);
typedef void (*readFilePtr)(struct MountPoint *mntPt, struct File *file, size_t dest_buffer, size_t file_offset, size_t length, size_t pml4,
	readFileCallback callback, void *callback_tag);

/* get the size of an opened file */
typedef void (*getFileSizeCallback)(size_t status, size_t size, void *tag);
typedef void (*getFileSizePtr)(struct MountPoint *mntPt, struct File *file, getFileSizeCallback callback, void *callback_tag);

/* close the file and release the file handle */
typedef void (*closeFileCallback)(size_t status, void *tag);
typedef void (*closeFilePtr)(struct MountPoint *mntPt, struct File *file, closeFileCallback callback, void *callback_tag);

/* close the mount point */
typedef bool (*unmountPtr)(struct MountPoint *mntPt);

/* count the entries in a directory */
typedef void (*countEntriesInDirectoryCallback)(size_t status, size_t entries, void *tag);
typedef void (*countEntriesInDirectoryPtr)(struct MountPoint *mntPt, char *path, size_t path_length, size_t entries_offset,
	countEntriesInDirectoryCallback callback, void *callback_tag);

/* read entries in a directory */
typedef void (*readEntriesInDirectoryCallback)(size_t status, size_t entries, void *tag);
typedef void (*readEntriesInDirectoryPtr)(struct MountPoint *mntPt, char *path, size_t path_length, struct DirectoryEntry *dest_buffer,
	size_t dest_buffer_size, size_t pml4, size_t entries_offset, readEntriesInDirectoryCallback callback, void *callback_tag);

struct StorageDevice;

struct MountPoint {
	char *path; /* must start and end with a /, dynamically allocated */
	unsigned short path_length;
	unsigned short parent_path_length; /* length of path up to the parent directory */
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
	void *tag;
};

#define DIRECTORYENTRY_TYPE_MOUNTPOINT 0
#define DIRECTORYENTRY_TYPE_DIRECTORY 1
#define DIRECTORYENTRY_TYPE_FILE 2

struct DirectoryEntry {
	char name[256];
	uint8 name_length;
	uint8 type;
	size_t size;
};

extern void init_vfs();
extern bool mount(struct MountPoint *mount_point);
extern bool unmount(char *mount_point_path, size_t path_length);

extern struct MountPoint *find_mount_point(char *path, size_t path_length);

extern void open_file(char *path, size_t path_length, openFileCallback callback, void *tag);
extern void close_file(struct File *file, closeFileCallback callback, void *tag);
extern void get_file_size(struct File *file, getFileSizeCallback callback, void *tag);
extern void read_file(struct File *file, size_t dest_buffer, size_t file_offset, size_t length, size_t pml4,
	readFileCallback callback, void *callback_tag);
extern void count_entries_in_directory(char *path, size_t path_length,
	countEntriesInDirectoryCallback callback, void *tag);
extern void read_entries_in_directory(char *path, size_t path_length, struct DirectoryEntry *dest_buffer, size_t dest_buffer_size, size_t pml4,
	readEntriesInDirectoryCallback callback, void *tag);
