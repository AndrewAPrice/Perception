System calls are invoked with the `syscall` instruction.

Registers `rcx` and `r11` are always clobbered during a system call. `rsp` is always preserved. If a register is not used in the input or output of a system call, then it is also preserved.

Listed below are the system calls.

# Debugging

## Print Debug Character

Prints a debug character. Currently, this character gets outputted via COM1.

### Input
* `rdi` - 0
* `rax` - ASCII character to print.

### Output
Nothing.

# Threading

# Create Thread

Creates a new thread and schedules it for execution.

- TODO - 2nd param?
- TODO set stack size.

### Input
* `rdi` - 1
* `rax` - Entry point to begin executing.
* `rbx` - Parameter to pass to the new thread.

### Output
* `rax` - The ID of the created thread, or 0 if the thread could not be created.

## Get this thread's ID

Gets the ID of the currently running thread.

### Input
* `rdi` - 2

### Output
* `rax` - The ID of the currently running thread.

## Sleep this thread

Puts the currently running thread to sleep. Only returns once the thread is woken.

### Input
* `rdi` - 3

### Output
Nothing.

## Sleep thread

Sleeps a thread. If the provided thread ID matches the currently running thread, then this only returns once the thread is woken.

### Input
* `rdi` - 9
* `rax` - The ID of the thread to put to sleep.

### Output
Nothing.

## Wake thread

Wakes thread up. TODO: if thread is waiting for event, set bad event id

### Input
* `rdi` - 10

### Output
Nothing.

## Wake and switch to thread

Wakes thread up and begin executing it immediately. TODO: if thread is waiting for event, set bad event id

### Input
* `rdi` - 10

### Output
Nothing.

## Terminate this thread

Terminates this thread and releases its stack. This system call will not return.

### Input
* `rdi` - 4

## Terminate thread

Terminates a thread and releases its stack. If the provided thread ID matches the currently running thread, then this system call will not return.

### Input
* `rdi` - 5
* `rax` - The thread to terminate running.

### Output
Nothing.

## Yield

Voluntarily hands execution over to the next scheduled thread.

### Input
* `rdi` - 8

### Output
Nothing.

## Set thread segment

Sets the value of the FS segment base. Each thread can have its own FS segment base.

### Input
* `rdi` - 27
* `rax` - The address of the fs segment base.

## Set memory address to clear on thread termination

Sets a memory address of a 64-bit (8-byte aligned) integer that should cleared when the currently running thread is terminated.

### Input
* `rdi` - 28
* `rax` - An 8-byte aligned addres to a 64-bit integer that should be cleared. A value of 0 disables this behavior.

# Memory management

## Allocate memory pages
Allocates a contiguous set of memory pages into the process. Memory pages are 4KB each.

### Input
* `rdi` - 12
* `rax` - Number of memory pages to allocate.

### Output
* `rax` - The address of the start of the set of memory pages, or 1 if no memory could be allocated.

## Release memory pages
Releases memory pages from the process back the operating system. Memory pages are 4KB each.

### Input
* `rdi` - 13
* `rax` - The address of the start of the set of memory pages.
* `rbx` - The number of memory pages to release.

### Output
Nothing.

## Map physical memory page
Maps a physical memory page into the process. Only drivers can call this.

### Input
* `rdi` - 41
* `rax` - The first physical address.
* `rbx` - The number of physical pages to map.

### Output
* `rax` - The starting address of the set of memory pages, or 1 if it could not be allocated.

## Get free system memory
Returns the amount of free memory on the system that has not yet been allocated.

### Input
* `rdi` - 14

### Output
* `rax` - The amount of free memory the system has, in bytes.

## Get memory used by process
Returns the amount of memory allocated to the currently running process.

### Input
* `rdi` - 15

### Output
* `rax` - The amount of memory allocated to this process, in bytes.

## Get total system memory
Returns the total amount of memory that the computer has.

### Input
* `rdi` - 16

### Output
* `rax` - The total amount of memory the computer has, in bytes.

## Create shared memory
Creates a shared memory block, and joins it into the process.

### Input
* `rdi` - 42
* `rax` - The size of the shared memory, in pages.

### Output
* `rax` - The ID of the shared memory block, or 0 if it could not be created.
* `rbx` - The address of the shared memory.

## Join shared memory.
Joins a shared memory block.

### Input
* `rdi` - 43
* `rax` - The ID of the shared memory block.

### Output
* `rax` - The size of the shared memory, in pages, or 0 if it could not be mapped.
* `rbx` - The address of the shared memory block.

## Leave shared memory.
Leaves a shared memory block. If there are no more references to the shared memory block then the memory is released from the system.

### Input
* `rdi` - 44
* `rax` - The ID of the shared memory block.

# Process Management

## Get this process's ID

Gets the ID of the currently running process.

### Input
* `rdi` - 39

### Output
* `rax` - The ID of the currently running process.

## Terminate this process

Terminates this process. This system call will not return.

### Input
* `rdi` - 6

## Terminate process

Terminates a process. If the provided process ID matches the currently running process, then this system call will not return.

TODO: Build a permission system.

### Input
* `rdi` - 7
* `rax` - Process ID to terminate.

### Output
Nothing.

## Get process by name

### Input
* `rdi` - 22
* `rbp` - Minimum process ID.
* `rax` - Char 0-7.
* `rbx` - Char 8-15.
* `rdx` - Char 16-23.
* `rsi` - Char 24-31.
* `r8` - Char 32-39.
* `r9` - Char 40-47.
* `r10` - Char 48-55.
* `r12` - Char 56-63.
* `r13` - Char 64-71.
* `r14` - Char 72-79.
* `r15` - Char 80-87.

### Output
* `rdi` - Number of processes found. If this is above 12 then there are multiple pages.
* `rbp` - Process ID 1.
* `rax` - Process ID 2.
* `rbx` - Process ID 3.
* `rdx` - Process ID 4.
* `rsi` - Process ID 5.
* `r8` - Process ID 6.
* `r9` - Process ID 7.
* `r10` - Process ID 8.
* `r12` - Process ID 9.
* `r13` - Process ID 10.
* `r14` - Process ID 11.
* `r15` - Process ID 12.

## Get name of process

### Input
* `rdi` - 29
* `rax` - The ID of the process.

### Output
* `rdi` - Was the process found?
* `rax` - Char 0-7.
* `rbx` - Char 8-15.
* `rdx` - Char 16-23.
* `rsi` - Char 24-31.
* `r8` - Char 32-39.
* `r9` - Char 40-47.
* `r10` - Char 48-55.
* `r12` - Char 56-63.
* `r13` - Char 64-71.
* `r14` - Char 72-79.
* `r15` - Char 80-87.

## Notify when process disappears

### Input
* `rdi` - 30
* `rax` - The ID of the process.
* `rbx` - The message ID to send when a process disappears.

## Stop notyfing when a process disappears

### Input
* `rdi` - 31
* `rax` - The message ID to no longer send.

### Output
Nothing.

# Services

## Register service

Registers the service
### Input
* `rdi` - 32
* `rbp` - The ID of the service.
* `rax` - Char 0-7.
* `rbx` - Char 8-15.
* `rdx` - Char 16-23.
* `rsi` - Char 24-31.
* `r8` - Char 32-39.
* `r9` - Char 40-47.
* `r10` - Char 48-55.
* `r12` - Char 56-63.
* `r13` - Char 64-71.
* `r14` - Char 72-79.

### Output
Nothing.

## Unregister service

Unregisters a service

### Input
* `rdi` - 33
* `rax` - The ID of the service to destroy.

### Output
Nothing.

## Get service by name

### Input
* `rdi` - 34
* `rbp` - Minimum process ID.
* `rax` - Minimum service ID within the minimum process ID.
* `rbx` - Char 0-7.
* `rdx` - Char 8-15.
* `rsi` - Char 16-23.
* `r8` - Char 24-31.
* `r9` - Char 32-39.
* `r10` - Char 40-47.
* `r12` - Char 48-55.
* `r13` - Char 56-63.
* `r14` - Char 64-71.
* `r15` - Char 72-79.

### Output
* `rdi` - Number of service found. If this is above 6 then there are multiple pages.
* `rbp` - Process ID 1.
* `rax` - Service ID 1.
* `rbx` - Process ID 2.
* `rdx` - Service ID 2.
* `rsi` - Process ID 3.
* `r8` - Service ID 3.
* `r9` - Process ID 4.
* `r10` - Service ID 4.
* `r12` - Process ID 5.
* `r13` - Service ID 5.
* `r14` - Process ID 6.
* `r15` - Service ID 6.

## Notify when service appears
Also sends an event for all existing notifications with this name.

### Input
* `rdi` - 35
* `rbp` - The event ID to send when the service appears.
* `rax` - Char 0-7.
* `rbx` - Char 8-15.
* `rdx` - Char 16-23.
* `rsi` - Char 24-31.
* `r8` - Char 32-39.
* `r9` - Char 40-47.
* `r10` - Char 48-55.
* `r12` - Char 56-63.
* `r13` - Char 64-71.
* `r14` - Char 72-79.

### Output
Nothing.

## Stop notyfing when service appears.

### Input
* `rdi` - 36
* `rax` - The message ID we no longer want to send.

## Notify when service disappears

Sends the calling process a message when a service or the owning process disappears.

### Input
* `rdi` - 37
* `rax` - The ID of the process.
* `rbx` - The ID of the service.
* `rdx` - The message ID to send when a process disappears.

### Output
Nothing.

## Stop notifying when a service dissapears

### Input
* `rdi` - 38
* `rax` - The message ID we no longer want to send.

# Messaging

## Send message

Sends a message to a process.

### Input
* `rdi` - 17
* `rax` - The ID of the message.
* `rbx` - The ID of the process to send the message to.
* `rdx` - Parameters bitfield:
	- Bit 0: Are we sending pages?
* `rsi` - The first parameter.
* `r8` - The second parameter.
* `r9` - The third parameter.
If rdx[0] is '1':
* `r10` - Address of the first memory page.
* `r12` - The number of pages to send.
If rdx[0] is '0':
* `r10` - The fourth parameter.
* `r12` - The fifth parameter.

If rdx[1] is '1':
* `rsi` - The message ID we want the callee to respond with.

### Output
* `rax` - The status, which may be:
  * 0 - The message was sent successfully.
  * 1 - The process doesn't exist.
  * 2 - The kernel is out of memory.
  * 3 - The receiving process's queue is full.
  * 4 - Messaging is unsupported on this platform.
  * 5 - We are attempting to send a page, but the address range wasn't valid.

The memory is left untouched for all statuses other than '0'.

## Poll for message

Polls for a message, and returns immediately regardless of if there is a message.

### Input
* `rdi` - 18

### Output
If there was a message queued:

* `rax` - The ID of the message.
* `rbx` - The ID of the process that sent the message.
* `rdx` - Parameters bitfield:
	- Bit 0: Are these pages?
* `rsi` - The first parameter.
* `r8` - The second parameter.
* `r9` - The third parameter.
If rdx[0] is '1':
* `r10` - Address of the first memory page.
* `r12` - The number of pages sent.
If rdx[0] is '0':
* `r10` - The fourth parameter.
* `r12` - The fifth parameter.

If rdx[1] is '1':
* `rsi` - The message ID to respond with.

If there were no messages queued:

* `rax` - 0xFFFFFFFFFFFFFFFF

## Sleep until message

Sleeps until a message.

### Input
* `rdi` - 19

### Output
Same as `Poll for message`. Except if there were no messages queued and the thread was woken for other reasons:

* `rax` - 0xFFFFFFFFFFFFFFFF

# Interrupts

## Register message to send on interrupt
Registers a message to be send to this process when an interrupt occurs. Only a driver may call this system call.

### Input
* `rdi` - 20
* `rax` - The interrupt's number.
* `rbx` - The ID of the message to send.

### Output
Nothing.

## Unregister message to send on interrupt
Unregisters a message to be sent to this process when an interrupt occurs. Only a driver may call this system call.

### Input
* `rdi` - 20
* `rax` - The interrupt's number.
* `rbx` - The ID of the message to send.

### Output
Nothing.

# Drivers

## Grab the multiboot framebuffer information.

### Input
* `rdi` - 40

### Output
* `rax` - The physical memory address of the framebuffer.
* `rbx` - The width of the framebuffer, in pixels.
* `rdx` - The height of the framebuffer, in pixels.
* `rsi` - The number of bytes in memory between rows of pixels.
* `r8` - The number of bits per pixel.

# Time

## Send message after x microseconds
Sends a message to the calling process after a specified number of microseconds have passed.

### Input
* `rdi` - 23
* `rax` - The number of microseconds until we send this message.
* `rbx` - The message ID to send after the elapsed time has passed.

## Send message at timestamp
Sends a message to the calling process at or after a specific timestamp.

### Input
* `rdi` - 24
* `rax` - The number of microseconds since the kernel started that we should send this message.
* `rbx` - The message ID to send after the elapsed time has passed.

## Get current timestamp
Returns the time (in microseconds) since the kernel started.

### Input
* `rdi` - 25

### Output
* `rax` - The number of microseconds since the kernel started.


### Output
Nothing.
