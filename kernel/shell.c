#include "callback.h"
#include "draw.h"
#include "font.h"
#include "liballoc.h"
#include "physical_allocator.h"
#include "scheduler.h"
#include "shell.h"
#include "storage_device.h"
#include "syscall.h"
#include "text_terminal.h"
#include "video.h"
#include "virtual_allocator.h"
#include "vfs.h"
#include "window_manager.h"

#if 0
void print_out_file() {
	size_t chunk_size = 1000;
	char *chunk = malloc(chunk_size);
	if(!chunk)
		return;

	char *path = "/cd1/README.md";

	print_string("\nFILE READING TEST\n\nFile: ");
	print_string(path);

	/* open the file */
	struct CallbackSyncParamTag tag_param;
	tag_param.thread = running_thread;
	tag_param.response = 0;
	open_file(path, strlen(path), (openFileCallback)callback_sync_param_handler, &tag_param);
	while(!tag_param.response) sleep_if_not_set(&tag_param.response);

	if(tag_param.status != VFS_STATUS_SUCCESS) {
		print_string("Error.\n");
		free(chunk);
		return;
	}

	struct File *file = (struct File *)tag_param.result;

	/* get the file size */
	size_t file_size;
	tag_param.response = 0;
	get_file_size(file, (getFileSizeCallback)callback_sync_param_handler, &tag_param);
	while(!tag_param.response) sleep_if_not_set(&tag_param.response);

	file_size = tag_param.result;

	print_string(" File size: ");
	print_number(file_size);
	print_string(" Contents: \n\n");

	size_t offset = 0;

	struct CallbackSyncTag tag;
	tag.thread = running_thread;

	while(file_size > 0) {
		size_t this_chunk = file_size > chunk_size ? chunk_size : file_size;

		/* read this part of the file */

		tag.response = 0;
		read_file(file, (size_t)chunk, offset, this_chunk, kernel_pml4, callback_sync_handler, &tag);
		while(!tag.response) sleep_if_not_set(&tag.response);

		if(tag.status == VFS_STATUS_SUCCESS)
			print_fixed_string(chunk, this_chunk);


		file_size -= this_chunk;
		offset += this_chunk;
	}

	free(chunk);
	/* close the file */
	tag.response = 0;
	close_file(file, callback_sync_handler, &tag);
	while(!tag.response) sleep_if_not_set(&tag.response);
}
#endif

#if 0
void print_out_directory() {
	char *path = "/cd1/boot/";
	print_string("\nDIRECTORY LISTING TEST\n\nPath: ");
	print_string(path);

	/* get the number of items in the directory */
	struct CallbackSyncParamTag tag_param;
	tag_param.thread = running_thread;
	tag_param.response = 0;
	count_entries_in_directory(path, strlen(path), (countEntriesInDirectoryCallback)callback_sync_param_handler, &tag_param);
	while(!tag_param.response) sleep_if_not_set(&tag_param.response);

	if(tag_param.status != VFS_STATUS_SUCCESS) {
		print_string(" Error listing directory\n");
		return;
	}
	size_t entries_count = tag_param.result;

	print_string(" Entries: ");
	print_number(entries_count);
	print_string("\n\n");

	struct DirectoryEntry *dir_entries = malloc(sizeof(struct DirectoryEntry) * entries_count);
	if(!dir_entries) return;

	/* get those entries */	
	tag_param.response = 0;
	read_entries_in_directory(path, strlen(path), dir_entries, sizeof(struct DirectoryEntry) * entries_count,
		kernel_pml4, (readEntriesInDirectoryCallback)callback_sync_param_handler, &tag_param);
	while(!tag_param.response) sleep_if_not_set(&tag_param.response);

	entries_count = tag_param.result;
	size_t i; for(i = 0; i < entries_count; i++) {
		print_char(' ');
		print_fixed_string(dir_entries[i].name, dir_entries[i].name_length);
		switch(dir_entries[i].type) {
			case DIRECTORYENTRY_TYPE_MOUNTPOINT:
				print_string(" - Mount Point");
				break;
			case DIRECTORYENTRY_TYPE_DIRECTORY:
				print_string(" - Directory");
				break;
			case DIRECTORYENTRY_TYPE_FILE:
				print_string(" - File");
				print_size(dir_entries[i].size);
				break;
		}
		print_char('\n');
	}
	free(dir_entries);
}
#endif

uint32 *shell_buffer;
uint8 shell_tab;
#define SHELL_TAB_LAUNCH 0
#define SHELL_TAB_RUNNING 1
uint16 shell_tab_launch_x, shell_tab_running_x;
char *shell_tab_launch_title = "Launch";
char *shell_tab_running_title = "Running";
#define SHELL_TAB_Y 10
#define SHELL_TEXT_OFFSET 40

#define SHELL_BACKGROUND_COLOUR 0xEE7092BE

#define FOCUSED_TAB_COLOUR 0xFFFFFFFF
#define UNFOCUSED_TAB_COLOUR 0xFF000000


/* draws the shell's background */
void shell_draw_background() {
	/* draw the left solid */
	fill_rectangle(0, 0, SHELL_WIDTH, screen_height, SHELL_BACKGROUND_COLOUR, shell_buffer, SHELL_WIDTH, screen_height);
}

/* draws the shell */
void shell_draw() {
	shell_draw_background();
	/* draw the tabs */
	draw_string_n(shell_tab_launch_x, SHELL_TAB_Y, shell_tab_launch_title,
		shell_tab == SHELL_TAB_LAUNCH ? FOCUSED_TAB_COLOUR : UNFOCUSED_TAB_COLOUR,
		shell_buffer, SHELL_WIDTH, screen_height);

	draw_string_n(shell_tab_running_x, SHELL_TAB_Y, shell_tab_running_title,
		shell_tab == SHELL_TAB_RUNNING ? FOCUSED_TAB_COLOUR : UNFOCUSED_TAB_COLOUR,
		shell_buffer, SHELL_WIDTH, screen_height);

	if(is_shell_visible)
		invalidate_window_manager(0, 0, SHELL_WIDTH, screen_height);
}

void init_shell() {
	shell_buffer = malloc(sizeof(uint32) * screen_height * SHELL_WIDTH);
	if(!shell_buffer) {
		print_string("No memory for the shell buffer!");
		asm("hlt");
	}

	shell_tab_launch_x = SHELL_WIDTH / 2 - SHELL_TEXT_OFFSET - measure_string_n(shell_tab_launch_title)/2;
	shell_tab_running_x = SHELL_WIDTH / 2 + SHELL_TEXT_OFFSET - measure_string_n(shell_tab_running_title)/2;

	shell_tab = SHELL_TAB_LAUNCH;

	/* draw the shell, so there's something in the buffer as soon as it first appears */
	shell_draw();
}


void shell_entry() {
	print_string("Entered the shell. Total memory:"); print_size(total_system_memory);

	size_t free_mem = free_pages * page_size;

	print_string(" Used:");	print_size(total_system_memory - free_mem);
	
	print_string(" Free:");	print_size(free_mem); print_char('\n');

	// wait a little bit for stuff to be mounted
	//size_t i; for (i = 0; i < 50; i++)
	//	asm("hlt");

	//print_out_directory();
	
	while(true) { sleep_thread(); asm("hlt"); };
}

/* notifies the shell that a disk was mounted */
void shell_disk_mounted() {
	print_string("A new disk was mounted!\n");
}

/* notifies the shell that a mouse button was pressed */
void shell_mouse_button_down(uint16 x, uint16 y, uint8 button) {

}

/* notifies the shell that a mouse button was released */
void shell_mouse_button_up(uint16 x, uint16 y, uint8 button) {

}

/* notifies the shell that the mouse moved */
void shell_mouse_move(uint16 x, uint16 y, uint8 button) {

}

/* notifies the shell that a key was pressed */
void shell_key_down(uint8 scancode) {

}

/* notifies the shell that it's now visible */
void shell_visible() {

}