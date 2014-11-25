#include "callback.h"
#include "liballoc.h"
#include "physical_allocator.h"
#include "scheduler.h"
#include "shell.h"
#include "storage_device.h"
#include "syscall.h"
#include "text_terminal.h"
#include "virtual_allocator.h"
#include "vfs.h"

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

void shell_entry() {
	print_string("Total memory:");
	print_size(total_system_memory);

	size_t free_mem = free_pages * page_size;

	print_string(" Used:");
	print_size(total_system_memory - free_mem);
	
	print_string(" Free:");
	print_size(free_mem);
	print_char('\n');

	// wait a little bit for stuff to be mounted
	size_t i; for (i = 0; i < 100; i++)
		asm("hlt");

	print_out_file();
	
	while(true) { sleep_thread(); asm("hlt");};
}