#ifndef PROCESS_H
#define PROCESS_H

struct TurkeyVM;
struct Window;
struct TurkeyString;

struct ProcessPermissions {
	bool launch_programs; /* can this process launch other programs? */
	bool send_messages; /* can this process send messages to other programs? */
	bool run_background; /* can this process run when it's not in focus? */
	/* file permissions */
	bool write_executables; /* can this process write to it's executable directory? */
	bool write_assets; /* can this process write to it's assets directory? */
	bool read_documents; /* can this process read your documents? */
	bool write_documents; /* can this process write your documents? */
	bool read_everything; /* can this process read everywhere? */
	bool write_everything; /* can this process write everywhere? */
};

struct KeyboardState {
	bool num_lock;
	bool scroll_lock;
	bool caps_lock;
};

struct Process {
	TurkeyVM *vm;
	ProcessPermissions permissions;
	Window *windows; /* linked list of windows */
	TurkeyString *name;
	TurkeyString *executing_path;

	KeyboardState keyboardState;

	size_t allocated_memory; /* amount of memory allocated */
	size_t memory_since_last_gc; /* amount of memory allocated since the last garbage collection */
	size_t maximum_memory; /* the maximum amount of memory a process can use */
};

struct ProcessLaunchInfo {
	char *name;
	unsigned int name_length;
};

extern void process_initialize();
extern Process *process_get_current();

extern void process_launch_process(char *name, unsigned int name_length); /* launch a process */
/* entry point from platform code for a new thread, thread will return from here when it's finished */
extern void process_thread_main(ProcessLaunchInfo *tag);

#endif
