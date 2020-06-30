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

# Process Management

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

# Messaging

## Send message

Sends a message to a process.

### Input
* `rdi` - 17
* `rax` - The ID of the message.
* `rsi` - The first parameter.
* `rdx` - The second parameter.
* `rbx` - The third parameter.
* `r8` - The fourth parameter.
* `r9` - The fifth parameter.
* `r10` - The ID of the process to send the message to.

### Output
* `rax` - The status, which may be:
  * 0 - The message was sent successfully.
  * 1 - The process doesn't exist.
  * 2 - The kernel is out of memory.
  * 3 - The receiving process's queue is full.

## Poll for message

Polls for a message, and returns immediately regardless of if there is a message.

### Input
* `rdi` - 18

### Output
If there was a message queued:

* `rax` - The ID of the message.
* `rsi` - The first parameter.
* `rdx` - The second parameter.
* `rbx` - The third parameter.
* `r8` - The fourth parameter.
* `r9` - The fifth parameter.
* `r10` - The ID of the process that sent this message.

If there were no messages queued:

* `rax` - 0xFFFFFFFFFFFFFFFF

## Sleep until message

Sleeps until a message.

### Input
* `rdi` - 18

### Output
If there was a message queued:

* `rax` - The ID of the message.
* `rsi` - The first parameter.
* `rdx` - The second parameter.
* `rbx` - The third parameter.
* `r8` - The fourth parameter.
* `r9` - The fifth parameter.
* `r10` - The ID of the process that sent this message.

If there were no messages queued and the thread was woken for other reasons:

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
