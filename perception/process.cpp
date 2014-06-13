#include <turkey.h>
#include "process.h"
#include "platform.h"

Process *running_processes;

void process_initialize() {
	running_processes = 0;
};

Process *process_get_current() {
	return 0;
};

void process_launch_process(char *name, unsigned int name_length) {
	ProcessLaunchInfo *launchInfo = (ProcessLaunchInfo *)platform_allocate_memory(sizeof ProcessLaunchInfo);
	launchInfo->name_length = name_length;
	launchInfo->name = (char *)platform_allocate_memory(name_length);
	platform_memory_copy(launchInfo->name, name, name_length);

	if(!platform_thread_create(launchInfo)) {
		platform_free_memory(launchInfo->name);
		platform_free_memory(launchInfo);
	}
}

/* entry point for a new thread */
void process_thread_main(ProcessLaunchInfo *tag) {
	platform_kernel_panic("In thread!");
}