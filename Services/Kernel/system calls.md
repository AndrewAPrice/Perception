# Perception Kernel System Calls

System calls are invoked using the x86_64 `syscall` instruction. The system call number is passed in register `rdi`.

## Registers & Calling Convention
- **System Call Number**: Passed in `rdi`.
- **Clobbered Registers**: `rcx` and `r11` are always clobbered by the `syscall` instruction execution.
- **Preserved Registers**: Unless specified as an input or output parameter for a particular system call, all other general-purpose registers are preserved by the kernel.

---

# System Calls Master Index

| Number (`rdi`) | System Call | Category | Description |
| :---: | :--- | :--- | :--- |
| `0` | [Print Debug Character](#print-debug-character) | Debugging & Diagnostics | Outputs an ASCII character to COM1. |
| `1` | [Create Thread](#create-thread) | Thread Management | Creates and schedules a new thread. |
| `2` | [Get This Thread's ID](#get-this-threads-id) | Thread Management | Returns the current thread's ID. |
| `3` | [Sleep This Thread](#sleep-this-thread) | Thread Management | Puts the current thread to sleep. |
| `4` | [Terminate This Thread](#terminate-this-thread) | Thread Management | Terminates the current thread. |
| `5` | [Terminate Thread](#terminate-thread) | Thread Management | Terminates a target thread by ID. |
| `6` | [Terminate This Process](#terminate-this-process) | Process Management | Terminates the calling process. |
| `7` | [Terminate Process](#terminate-process) | Process Management | Terminates a process by PID. |
| `8` | [Set System Message Handlers](#set-system-message-handlers) | Inter-Process Communication (IPC) | Registers system event message handlers. |
| `9` | [Sleep Thread](#sleep-thread) | Thread Management | Puts a specified thread to sleep. |
| `10` | [Wake Thread](#wake-thread) | Thread Management | Wakes up a sleeping thread. |
| `11` | [Wait and Switch to Thread](#wait-and-switch-to-thread) | Thread Management | Wakes up and immediately yields to a thread. |
| `12` | [Allocate Memory Pages](#allocate-memory-pages) | Memory Management | Allocates virtual memory pages. |
| `13` | [Release Memory Pages](#release-memory-pages) | Memory Management | Releases virtual memory pages back to OS. |
| `14` | [Get System Memory Metrics](#get-system-memory-metrics) | Memory Management | Queries total, shared, and free memory. |
| `15` | [Get Process Health Metrics](#get-process-health-metrics) | Memory Management | Queries memory, CPU usage, and thread metrics for a process. |
| `16` | *Unassigned* | — | Reserved / Unused. |
| `17` | [Send Message](#send-message) | Inter-Process Communication (IPC) | Delivers an IPC message to a target process. |
| `18` | [Poll for Message](#poll-for-message) | Inter-Process Communication (IPC) | Non-blocking retrieval of queued IPC messages. |
| `19` | [Sleep Until Message](#sleep-until-message) | Inter-Process Communication (IPC) | Blocks thread execution until an IPC message arrives. |
| `20` | [Register Message to Send on Interrupt](#register-message-to-send-on-interrupt) 🔒 | Hardware & Drivers | Binds an IRQ to a process IPC message. |
| `21` | [Unregister Message to Send on Interrupt](#unregister-message-to-send-on-interrupt) 🔒 | Hardware & Drivers | Removes an IRQ IPC message binding. |
| `22` | [Get Processes](#get-processes) | Process Management | Iterates over running system processes. |
| `23` | [Send Message After X Microseconds](#send-message-after-x-microseconds) | Time & Timers | Schedules a delayed IPC message timer. |
| `24` | [Send Message at Timestamp](#send-message-at-timestamp) | Time & Timers | Schedules an IPC message at an absolute timestamp. |
| `25` | [Get Current Timestamp](#get-current-timestamp) | Time & Timers | Returns microseconds elapsed since kernel boot. |
| `26` | [Print Registers and Stack](#print-registers-and-stack) | Debugging & Diagnostics | Dumps registers and stack to COM1 for debugging. |
| `27` | [Set Thread Segment](#set-thread-segment) | Thread Management | Configures the `%fs_base` register for TLS. |
| `28` | [Set Address to Clear on Thread Termination](#set-address-to-clear-on-thread-termination) | Thread Management | Sets a futex memory address to zero out on thread exit. |
| `29` | [Get Name of Process](#get-name-of-process) | Process Management | Retrieves the string name of a process by PID. |
| `30` | [Notify When Process Disappears](#notify-when-process-disappears) | Process Management | Requests an IPC notification on process exit. |
| `31` | [Stop Notifying When Process Disappears](#stop-notifying-when-process-disappears) | Process Management | Cancels process termination notifications. |
| `32` | [Register Service](#register-service) | Service Discovery & Registry | Registers a system service by name. |
| `33` | [Unregister Service](#unregister-service) | Service Discovery & Registry | Unregisters a system service. |
| `34` | [Get Services](#get-services) | Service Discovery & Registry | Searches registered services by name. |
| `35` | [Notify When Service Appears](#notify-when-service-appears) | Service Discovery & Registry | Requests notification when a service registers. |
| `36` | [Stop Notifying When Service Appears](#stop-notifying-when-service-appears) | Service Discovery & Registry | Cancels service appearance notifications. |
| `37` | [Notify When Service Disappears](#notify-when-service-disappears) | Service Discovery & Registry | Requests notification when a service unregisters. |
| `38` | [Stop Notifying When Service Disappears](#stop-notifying-when-service-disappears) | Service Discovery & Registry | Cancels service disappearance notifications. |
| `39` | [Get This Process's ID](#get-this-processs-id) | Process Management | Returns the current process ID. |
| `40` | [Grab Multiboot Framebuffer Information](#grab-multiboot-framebuffer-information) | Hardware & Drivers | Queries multiboot graphics display settings. |
| `41` | [Map Physical Memory Page](#map-physical-memory-page) 🔒 | Memory Management | Maps raw physical MMIO memory into address space. |
| `42` | [Create Shared Memory](#create-shared-memory) | Memory Management | Creates a shared memory block. |
| `43` | [Join Shared Memory](#join-shared-memory) | Memory Management | Maps a shared memory block into calling process. |
| `44` | [Leave Shared Memory](#leave-shared-memory) | Memory Management | Unmaps a shared memory block from calling process. |
| `45` | [Move Page into Shared Memory](#move-page-into-shared-memory) 🔑 | Memory Management | Transposes a virtual page into shared memory. |
| `46` | [Is Shared Memory Page Allocated](#is-shared-memory-page-allocated) | Memory Management | Checks if a shared memory offset has physical backing. |
| `47` | [Get Name of Service](#get-name-of-service) | Service Discovery & Registry | Queries the string name of a service ID. |
| `48` | [Set Memory Access Rights](#set-memory-access-rights) | Memory Management | Updates page read/write/executable permissions. |
| `49` | [Allocate Memory Pages Below Physical Address](#allocate-memory-pages-below-physical-address) 🔒 | Memory Management | Allocates physical pages below a bounded physical address. |
| `50` | [Get Physical Address of Virtual Address](#get-physical-address-of-virtual-address) 🔒 | Memory Management | Translates a virtual address to physical address. |
| `51` | [Create Process](#create-process) ⚙️ | Process Management | Instantiates a process in `creating` state. |
| `52` | [Set Child Process Memory Page](#set-child-process-memory-page) 👶 | Process Management | Transfers virtual memory page into a child process. |
| `53` | [Start Execution Process](#start-execution-process) 👶 | Process Management | Launches thread in `creating` child process. |
| `54` | [Destroy Child Process](#destroy-child-process) 👶 | Process Management | Cancels and frees an unlaunched child process. |
| `55` | [Enable Profiling](#enable-profiling) | Profiling & CPU Tracking | Starts recording CPU cycle metrics. |
| `56` | [Disable and Output Profiling](#disable-and-output-profiling) | Profiling & CPU Tracking | Stops profiling and outputs cycle report to COM1. |
| `57` | [Grant Permission to Allocate into Shared Memory](#grant-permission-to-allocate-into-shared-memory) 🔑 | Memory Management | Authorizes another PID to mutate shared memory. |
| `58` | [Get Shared Memory Details](#get-shared-memory-details) | Memory Management | Returns capacity and permission bitfield of shared memory. |
| `59` | [Get Shared Memory Page Physical Address](#get-shared-memory-page-physical-address) 🔒 | Memory Management | Queries physical address backing shared memory. |
| `60` | [Get Multiboot Module](#get-multiboot-module) 📦 | Process Management | Retrieves initial multiboot modules into memory. |
| `61` | [Join Child Process in Shared Memory](#join-child-process-in-shared-memory) 👶 | Memory Management | Maps shared memory into a child process. |
| `62` | [Grow Shared Memory](#grow-shared-memory) 🔑 | Memory Management | Expands capacity of a shared memory block. |
| `63` | [Set Thread Segment Extended](#set-thread-segment-extended) | Thread Management | Sets `%fs_base` and `%gs_base` segment registers. |
| `64` | [Set That Process Cares About CPU Tracking](#set-that-process-cares-about-cpu-tracking) | Profiling & CPU Tracking | Subscribes/unsubscribes process to rolling CPU usage metrics. |
| `65` | [Set Thread Priority](#set-thread-priority) | Scheduling & Priority | Configures scheduler priority level of a thread. |
| `66` | [Set Focused Process](#set-focused-process) 🛡️ | Process Management | Marks foreground process for scheduling priority boost. |
| `67` | [Get Time Info](#get-time-info) | Time & Timers | Retrieves UTC clock offset and TSC frequency multiplier. |
| `68` | [Set Time Info](#set-time-info) 🔒 | Time & Timers | Updates kernel base UTC clock offset. |
| `69` | [Register Message for When Time Info Changes](#register-message-for-when-time-info-changes) | Time & Timers | Requests IPC notification when UTC clock changes. |
| `70` | [Register Shared Memory Event](#register-shared-memory-event) | Synchronization Events | Binds shared memory offset mutation to IPC notification. |
| `71` | [Unregister Shared Memory Event](#unregister-shared-memory-event) | Synchronization Events | Removes shared memory offset event subscription. |
| `72` | [Trigger Shared Memory Event](#trigger-shared-memory-event) | Synchronization Events | Fires notification events on a shared memory offset. |

Restrictions:  
🔒 Only drivers may call this.  
🛡️ Only the window manager may call this.  
⚙️ Only processes with process creation permissions may call this.  
📦 Only the initial loader process may call this.  
👶 Only the parent/creator process may call this while the child is in the creating state.  
🔑 Only the creator or authorized writer of the shared memory block may call this.

---

# 1. Debugging & Diagnostics

## Print Debug Character
Prints a single debug character via COM1 serial output.

### Input
* `rdi` - `0`
* `rax` - ASCII character code to print.

### Output
Nothing.

---

## Print Registers and Stack
Prints the current executing thread's registers, stack trace, and execution state to COM1 serial output.

### Input
* `rdi` - `26`

### Output
Nothing.

---

# 2. Thread Management

## Create Thread
Creates a new execution thread within the calling process and schedules it for execution.

### Input
* `rdi` - `1`
* `rax` - Instruction pointer address where execution begins.
* `rbx` - Parameter argument passed to the new thread.
* `rdx` - Optional custom stack pointer (pass `0` to allocate a standard 32KB stack in the kernel).
* `rsi` - Optional thread-local storage (TLS) base address (pass `0` if none).

### Output
* `rax` - ID of the created thread, or `0` if thread creation failed.

---

## Get This Thread's ID
Returns the unique thread identifier of the currently executing thread.

### Input
* `rdi` - `2`

### Output
* `rax` - Thread ID of the calling thread.

---

## Sleep This Thread
Puts the calling thread to sleep. Execution halts until another thread explicitly wakes it.

### Input
* `rdi` - `3`

### Output
Nothing.

---

## Sleep Thread
Puts a specified target thread to sleep. If the target thread ID matches the calling thread, execution pauses until woken.

### Input
* `rdi` - `9`
* `rax` - Target thread ID to suspend.

### Output
Nothing.

---

## Wake Thread
Wakes up a sleeping target thread, adding it back to the active execution runqueue.

### Input
* `rdi` - `10`
* `rax` - Target thread ID to wake up.

### Output
Nothing.

---

## Wait and Switch to Thread
Wakes up a target thread and immediately yields execution to it.

### Input
* `rdi` - `11`
* `rax` - Target thread ID to wake and switch to.

### Output
Nothing.

---

## Terminate This Thread
Terminates the currently executing thread and releases its stack resources. This system call does not return.

### Input
* `rdi` - `4`

### Output
Does not return.

---

## Terminate Thread
Terminates a specified target thread and frees its stack resources. If the target thread ID is the current thread, this call does not return.

### Input
* `rdi` - `5`
* `rax` - Target thread ID to terminate.

### Output
Nothing (does not return if terminating current thread).

---

## Set Thread Segment
Sets the `%fs_base` segment base register for the calling thread, configuring thread-local storage (TLS).

### Input
* `rdi` - `27`
* `rax` - Base virtual address for the `%fs_base` segment register.

### Output
Nothing.

---

## Set Thread Segment Extended
Configures `%fs_base` and/or `%gs_base` segment registers for the calling thread in a single operation.

### Input
* `rdi` - `63`
* `rax` - Base address for `%fs_base` (applied if bit 0 of `rdx` is set).
* `rbx` - Base address for `%gs_base` (applied if bit 1 of `rdx` is set).
* `rdx` - **Bitfield Mask:**
  - Bit 0: Update `%fs_base`
  - Bit 1: Update `%gs_base`

### Output
Nothing.

---

## Set Address to Clear on Thread Termination
Registers an 8-byte aligned 64-bit integer address in user memory that the kernel automatically clears (sets to `0`) when the thread terminates. The kernel will also wake any threads blocked on a futex at this address.

### Input
* `rdi` - `28`
* `rax` - 8-byte aligned virtual memory address of integer to clear (pass `0` to disable).

### Output
Nothing.

---

# 3. Process Management

## Get This Process's ID
Returns the process ID (PID) of the currently running process.

### Input
* `rdi` - `39`

### Output
* `rax` - Process ID of the caller.

---

## Terminate This Process
Terminates the calling process and all its threads, releasing all allocated process resources. This call does not return.

### Input
* `rdi` - `6`

### Output
Does not return.

---

## Terminate Process
Terminates a process by its PID.

### Input
* `rdi` - `7`
* `rax` - Process ID of the process to terminate.

### Output
Nothing.

---

## Get Processes
Iterates over active processes in the system. Filter by process name by passing name characters in registers.

### Input
* `rdi` - `22`
* `r15` - Minimum Process ID lower bound to query.
* `rax`..`r14` - 10 registers (`rax`, `rbx`, `rdx`, `rsi`, `r8`, `r9`, `r10`, `r12`, `r13`, `r14`) holding up to 80 ASCII characters representing the target process name filter (leave empty for all processes).

### Output
* `rdi` - Total count of processes matching query.
* `r15`..`r14` - Up to 11 process IDs returned per query page.

---

## Get Name of Process
Retrieves the ASCII name string of a specified process ID.

### Input
* `rdi` - `29`
* `rax` - Target Process ID.

### Output
* `rdi` - Process found flag (`1` if found, `0` if not found).
* `rax`..`r15` - 11 registers returning up to 88 ASCII characters of the process name.

---

## Notify When Process Disappears
Registers an IPC notification message to be delivered to the caller when a target process terminates.

### Input
* `rdi` - `30`
* `rax` - Target Process ID to monitor.
* `rbx` - Message ID to deliver upon target process termination.

### Output
Nothing.

---

## Stop Notifying When Process Disappears
Cancels a previously registered process termination IPC notification.

### Input
* `rdi` - `31`
* `rax` - Message ID to unregister.

### Output
Nothing.

---

## Create Process ⚙️
Instantiates a new process structure in the `creating` state. The child process will not begin execution until explicitly launched. Only processes with process creation permissions may call this.

### Input
* `rdi` - `51`
* `rax` - **Permission Bitfield:**
  - Bit 0: Process has driver privilege rights.
  - Bit 1: Process has permission to spawn child processes.
* `rbx`..`r15` - 10 registers holding up to 80 ASCII characters defining the new process name.

### Output
* `rax` - Process ID of the created process, or `0` on failure.

---

## Set Child Process Memory Page 👶
Unmaps a virtual memory page from the calling process and transfers it into a child process in the `creating` state. Only the parent/creator process may call this while the child is in the creating state.

### Input
* `rdi` - `52`
* `rax` - Child Process ID.
* `rbx` - Source virtual page address in calling process.
* `rdx` - Destination virtual page address in child process.

### Output
Nothing.

---

## Start Execution Process 👶
Launches execution of a process currently in the `creating` state by creating its initial thread. Only the parent/creator process may call this while the child is in the creating state.

### Input
* `rdi` - `53`
* `rax` - Child Process ID.
* `rbx` - Entry point virtual instruction pointer address.
* `rdx` - Parameter argument passed to process entry point.

### Output
Nothing.

---

## Destroy Child Process 👶
Destroys and releases resources for an unlaunched process currently in the `creating` state. Only the parent/creator process may call this while the child is in the creating state.

### Input
* `rdi` - `54`
* `rax` - Child Process ID to destroy.

### Output
Nothing.

---

## Get Multiboot Module 📦
Retrieves initial boot multiboot modules loaded by the bootloader and transfers module memory into the caller's virtual space. Only the initial loader process may call this.

### Input
* `rdi` - `60`

### Output
* `rdi` - Virtual address where multiboot module was mapped (4KB aligned; lower 16 bits encode permission flags):
  - Bit 0: Process has driver permissions.
  - Bit 1: Process has process creation permissions.
* `r15` - Size of multiboot module in bytes.
* `rax`..`r14` - 10 registers containing the module/process string name.

---

## Set Focused Process 🛡️
Designates the process currently receiving user input focus (foreground). Threads in the focused process with `Normal` priority are temporarily elevated to `InteractiveApp` priority. Only the window manager may call this.

### Input
* `rdi` - `66`
* `rax` - Target Process ID to focus (`0` to clear focus).

### Output
* `rax` - Status code:
  - `0` - Success.
  - `1` - Process does not exist.
  - `2` - Access denied (caller is not Window Manager).

---

# 4. Scheduling & Priority

## Set Thread Priority
Sets the execution scheduling priority level of a specified thread.

### Input
* `rdi` - `65`
* `rax` - Target thread ID (`0` for current thread).
* `rbx` - **Priority Level (0–5):**
  - `0` - `InterruptDriver` (preempts all, requires driver permission 🔒)
  - `1` - `RealtimeService` (preempts standard applications)
  - `2` - `InteractiveApp` (high-responsiveness foreground tier)
  - `3` - `Normal` (standard application priority tier)
  - `4` - `Background` (low-priority background tasks)
  - `5` - `Idle` (executes only when no higher priority threads are awake)

### Output
* `rax` - Status code:
  - `0` - Success.
  - `1` - Invalid thread ID.
  - `2` - Invalid priority level.
  - `3` - Access denied.

---

# 5. Memory Management

## Virtual & Physical Allocation

### Allocate Memory Pages
Allocates contiguous 4KB virtual memory pages into the process virtual address space.

#### Input
* `rdi` - `12`
* `rax` - Number of 4KB memory pages to allocate.

#### Output
* `rax` - Starting virtual memory address of allocated pages.

---

### Release Memory Pages
Releases allocated 4KB virtual memory pages back to the operating system.

#### Input
* `rdi` - `13`
* `rax` - Starting virtual memory address.
* `rbx` - Number of 4KB memory pages to release.

#### Output
Nothing.

---

### Set Memory Access Rights
Modifies read, write, and execute permissions for virtual memory pages owned by the calling process.

#### Input
* `rdi` - `48`
* `rax` - Starting virtual page address.
* `rbx` - Number of pages.
* `rdx` - **Access Rights Bitfield:**
  - Bit 0: Writable (`1` = Read/Write, `0` = Read-Only)
  - Bit 1: Executable (`1` = Executable)

#### Output
Nothing.

---

### Allocate Memory Pages Below Physical Address 🔒
Allocates contiguous physical memory pages guaranteed to be located below a specified physical memory boundary. Only drivers may call this.

#### Input
* `rdi` - `49`
* `rax` - Number of 4KB pages to allocate.
* `rbx` - Maximum upper physical memory address bound.

#### Output
* `rax` - Starting virtual address (or `1` on allocation failure).
* `rbx` - Physical memory address of first page.

---

### Map Physical Memory Page 🔒
Maps raw physical hardware memory addresses directly into virtual address space. Only drivers may call this.

#### Input
* `rdi` - `41`
* `rax` - Physical memory starting address.
* `rbx` - Number of 4KB pages to map.

#### Output
* `rax` - Starting virtual address (or `1` on mapping failure).

---

### Get Physical Address of Virtual Address 🔒
Translates a process virtual memory address to its underlying physical memory address. Only drivers may call this.

#### Input
* `rdi` - `50`
* `rax` - Virtual memory address.

#### Output
* `rax` - Physical memory address.

---

## Shared Memory

### Create Shared Memory
Allocates a shared memory object and maps it into the calling process.

#### Input
* `rdi` - `42`
* `rax` - Size of shared memory block in 4KB pages.
* `rbx` - **Parameters Bitfield:**
  - Bit 0: Lazily allocated shared memory (pages created on demand).
  - Bit 1: Allow non-creator processes to write to shared memory.
* `rdx` - Message ID sent to creator when a lazily allocated page is accessed.

#### Output
* `rax` - Shared Memory Handle ID (or `0` on creation failure).
* `rbx` - Virtual memory address in calling process.

---

### Join Shared Memory
Maps an existing shared memory block into the calling process.

#### Input
* `rdi` - `43`
* `rax` - Shared Memory Handle ID.

#### Output
* `rax` - Size of shared memory block in pages (or `0` if mapping failed).
* `rbx` - Virtual memory address in calling process.
* `rdx` - Creation flags bitfield.

---

### Join Child Process in Shared Memory 👶
Maps a shared memory block into a child process in the `creating` state at a specified virtual address. Only the parent/creator process may call this while the child is in the creating state.

#### Input
* `rdi` - `61`
* `rax` - Child Process ID.
* `rbx` - Shared Memory Handle ID.
* `rdx` - Destination virtual address in child process.

#### Output
* `rax` - `1` if child joined successfully, `0` otherwise.

---

### Leave Shared Memory
Unmaps a shared memory block from the calling process. Memory is freed when all references leave.

#### Input
* `rdi` - `44`
* `rax` - Shared Memory Handle ID.

#### Output
Nothing.

---

### Get Shared Memory Details
Queries capacity and permission capabilities of a shared memory block.

#### Input
* `rdi` - `58`
* `rax` - Shared Memory Handle ID.

#### Output
* `rax` - **Capabilities Bitfield:**
  - Bit 0: Shared memory block exists.
  - Bit 1: Calling process has write permission.
  - Bit 2: Shared memory block is lazily allocated.
  - Bit 3: Calling process can assign pages to block.
* `rbx` - Total size of shared memory in bytes.

---

### Move Page into Shared Memory 🔑
Moves a virtual memory page into a shared memory block. Only the creator of the shared memory block may call this.

#### Input
* `rdi` - `45`
* `rax` - Shared Memory Handle ID.
* `rbx` - Offset within shared memory block in bytes.
* `rdx` - Source virtual page address to move.

#### Output
Nothing.

---

### Grant Permission to Allocate into Shared Memory 🔑
Grants a target process permission to allocate pages into a shared memory block. Only the creator of the shared memory block may call this.

#### Input
* `rdi` - `57`
* `rax` - Shared Memory Handle ID.
* `rbx` - Target Process ID.

#### Output
Nothing.

---

### Is Shared Memory Page Allocated
Checks whether a specific page offset within a shared memory block is physically backed.

#### Input
* `rdi` - `46`
* `rax` - Shared Memory Handle ID.
* `rbx` - Byte offset within shared memory block.

#### Output
* `rax` - `1` if page exists/allocated, `0` otherwise.

---

### Get Shared Memory Page Physical Address 🔒
Returns the underlying physical memory address backing a shared memory page. Only drivers may call this.

#### Input
* `rdi` - `59`
* `rax` - Shared Memory Handle ID.
* `rbx` - Byte offset within shared memory block.

#### Output
* `rax` - Physical memory address (or `1` if page is not allocated).

---

### Grow Shared Memory 🔑
Expands a shared memory block to at least the specified size in pages. Only the creator or an authorized writer process may call this.

#### Input
* `rdi` - `62`
* `rax` - Shared Memory Handle ID.
* `rbx` - Minimum target capacity in 4KB pages.

#### Output
* `rax` - New size of shared memory block in pages.
* `rbx` - Updated virtual memory address.

---

## Memory & Process Health Metrics

### Get System Memory Metrics
Queries global RAM utilization metrics across the operating system.

#### Input
* `rdi` - `14`

#### Output
* `rax` - Total system RAM capacity in bytes.
* `rbx` - Total allocated shared memory in bytes.
* `rdx` - Total free memory available in bytes.

---

### Get Process Health Metrics
Queries runtime resource health metrics (RAM, CPU, thread metrics) for a target process.

#### Input
* `rdi` - `15`
* `rax` - Target Process ID (`0` for current process).

#### Output
* `rax` - Unique private memory allocated to process in bytes (`0` if PID invalid).
* `rbx` - Microsecond timestamp when process was created (`0` if PID invalid).
* `rdx` - Compact CPU usage bitfield per core (`0` if PID invalid).
* `rsi` - Count of services registered by process (`0` if PID invalid).
* `rdi` - Shared memory allocated to process in bytes (`0` if PID invalid).

---

# 6. Inter-Process Communication (IPC)

## Set System Message Handlers
Registers system event message handlers for kernel events.

### Input
* `rdi` - `8`
* `rax` - Message ID delivered when a thread's TID clear-on-exit address is cleared (`0` = disable handler).

### Output
Nothing.

---

## Send Message
Delivers an IPC message to a specified destination process.

### Input
* `rdi` - `17`
* `rax` - Message ID identifier.
* `rbx` - Destination Process ID.
* `rdx` - **Message Type & Parameters Bitfield:**
  - Bits 0-1: Message Type
    - `00`: One-way message
    - `01`: Synchronous/call message expecting response
    - `10`: Response message
    - `11`: Invalid
* `rsi` - Parameter 1 (or Response Message ID if type is `01`).
* `r8` - Parameter 2.
* `r9` - Parameter 3.
* `r10` - Parameter 4.
* `r12` - Parameter 5.

### Output
* `rax` - Delivery Status Code:
  - `0` - Message delivered successfully.
  - `1` - Destination process does not exist.
  - `2` - Kernel out of memory.
  - `3` - Destination process message queue is full.
  - `4` - Messaging unsupported on platform.
  - `5` - Invalid memory address range.

---

## Poll for Message
Non-blocking check for queued incoming IPC messages.

### Input
* `rdi` - `18`

### Output
**If message is present in queue:**
* `rax` - Message ID.
* `rbx` - Sender Process ID.
* `rdx` - Message type bitfield.
* `rsi` - Parameter 1 (or response message ID).
* `r8`..`r12` - Parameters 2 through 5.

**If no message is queued:**
* `rax` - `0xFFFFFFFFFFFFFFFF`

---

## Sleep Until Message
Blocks thread execution until an incoming IPC message arrives in the queue.

### Input
* `rdi` - `19`

### Output
Same returns as `Poll for Message`. If thread is woken for non-message reasons with empty queue:
* `rax` - `0xFFFFFFFFFFFFFFFF`

---

# 7. Service Discovery & Registry

## Register Service
Registers a named system service for public discovery.

### Input
* `rdi` - `32`
* `r15` - Service ID within process.
* `rax`..`r14` - 10 registers holding up to 80 ASCII characters of the service name.

### Output
Nothing.

---

## Unregister Service
Unregisters a previously registered service.

### Input
* `rdi` - `33`
* `rax` - Service ID to unregister.

### Output
Nothing.

---

## Get Services
Queries active services registered in the system matching a name filter.

### Input
* `rdi` - `34`
* `rax` - Minimum Process ID search bound.
* `rbx` - Minimum Service ID search bound.
* `rdx`..`r15` - 10 registers defining service name string filter (empty = match all).

### Output
* `rdi` - Count of matching services found.
* `rax`/`rbx`..`r13`/`r14` - Pairs of [Process ID, Service ID] (up to 5 service pairs per page).

---

## Get Name of Service
Retrieves the registered string name of a service.

### Input
* `rdi` - `47`
* `rax` - Process ID.
* `rbx` - Service ID.

### Output
* `rdi` - Service found flag (`1` if found, `0` if not).
* `rax`..`r14` - 10 registers returning ASCII service name string.

---

## Notify When Service Appears
Requests IPC notification whenever a matching service name registers. Also delivers immediate events for existing services.

### Input
* `rdi` - `35`
* `r15` - Event Message ID to deliver upon appearance.
* `rax`..`r14` - 10 registers defining service name filter string.

### Output
Nothing.

---

## Stop Notifying When Service Appears
Cancels service appearance IPC notification.

### Input
* `rdi` - `36`
* `rax` - Message ID to unregister.

### Output
Nothing.

---

## Notify When Service Disappears
Requests IPC notification when a target service or its owner process unregisters or terminates.

### Input
* `rdi` - `37`
* `rax` - Target Process ID.
* `rbx` - Target Service ID.
* `rdx` - Message ID to deliver upon disappearance.

### Output
Nothing.

---

## Stop Notifying When Service Disappears
Cancels service disappearance IPC notification.

### Input
* `rdi` - `38`
* `rax` - Message ID to unregister.

### Output
Nothing.

---

# 8. Synchronization Events

## Register Shared Memory Event
Registers a notification event on a shared memory block offset. When another process triggers an event at this offset, the kernel delivers the registered message ID.

### Input
* `rdi` - `70`
* `rax` - Shared Memory Handle ID.
* `rbx` - Offset within shared memory block.
* `rdx` - Unique Message ID to deliver.

### Output
Nothing.

---

## Unregister Shared Memory Event
Unregisters a shared memory event subscription.

### Input
* `rdi` - `71`
* `rax` - Shared Memory Handle ID.
* `rbx` - Offset within shared memory block.

### Output
Nothing.

---

## Trigger Shared Memory Event
Triggers a shared memory event at a specified offset, waking all registered processes by delivering their one-shot notification messages.

### Input
* `rdi` - `72`
* `rax` - Shared Memory Handle ID.
* `rbx` - Offset within shared memory block.

### Output
Nothing.

---

# 9. Time & Timers

## Send Message After X Microseconds
Schedules an IPC message to be delivered to the calling process after a specified relative delay.

### Input
* `rdi` - `23`
* `rax` - Delay duration in microseconds.
* `rbx` - Message ID to deliver upon timer expiration.

### Output
Nothing.

---

## Send Message at Timestamp
Schedules an IPC message to be delivered to the calling process at an absolute kernel uptime timestamp.

### Input
* `rdi` - `24`
* `rax` - Absolute timestamp (microseconds since kernel boot).
* `rbx` - Message ID to deliver.

### Output
Nothing.

---

## Get Current Timestamp
Returns current system uptime in microseconds since kernel boot.

### Input
* `rdi` - `25`

### Output
* `rax` - Uptime in microseconds.

---

## Get Time Info
Queries current UTC time offset and TSC cycle-to-microsecond conversion multiplier.

### Input
* `rdi` - `67`

### Output
* `rax` - UTC time offset in microseconds.
* `rbx` - TSC cycle multiplier bits (C++ `double` bit representation).

---

## Set Time Info 🔒
Updates the base UTC wall clock time relative to TSC cycles. Only drivers may call this.

### Input
* `rdi` - `68`
* `rax` - Current UTC time in microseconds.

### Output
Nothing.

---

## Register Message for When Time Info Changes
Registers an IPC message delivered whenever the system UTC time info changes.

### Input
* `rdi` - `69`
* `rax` - Message ID to deliver (Message parameters receive: `param1` = UTC offset, `param2` = TSC multiplier).

### Output
Nothing.

---

# 10. Hardware & Drivers

## Register Message to Send on Interrupt 🔒
Binds a hardware interrupt (IRQ) to an IPC message delivered to the calling driver process. Only drivers may call this.

### Input
* `rdi` - `20`
* `rax` - Hardware Interrupt (IRQ) number.
* `rbx` - Message ID to deliver on interrupt.
* `rdx` - **Processing Mode:**
  - `0` - Send message on each interrupt.
  - `1` - Poll and batch read bytes from hardware port while status matches mask.
    - `rsx` - Port and mask settings:
      - Bits 0-15: Status port
      - Bits 16-31: Read port
      - Bits 32-39: Status bitmask to match

### Output
Nothing.

---

## Unregister Message to Send on Interrupt 🔒
Removes a hardware interrupt IPC message binding. Only drivers may call this.

### Input
* `rdi` - `21`
* `rax` - Hardware Interrupt (IRQ) number.
* `rbx` - Message ID to unbind.

### Output
Nothing.

---

## Grab Multiboot Framebuffer Information
Retrieves physical memory address and display layout specifications for the boot multiboot framebuffer.

### Input
* `rdi` - `40`

### Output
* `rax` - Starting physical address of display framebuffer.
* `rbx` - Display width in pixels.
* `rdx` - Display height in pixels.
* `rsi` - Pitch (stride in bytes between pixel rows).
* `r8` - Color depth (bits per pixel).

---

# 11. Profiling & CPU Tracking

## Enable Profiling
Starts recording CPU execution cycles spent across user processes, system calls, and kernel routines.

### Input
* `rdi` - `55`

### Output
Nothing.

---

## Disable and Output Profiling
Stops profiling and outputs recorded CPU cycle profiling statistics to COM1 serial console.

### Input
* `rdi` - `56`

### Output
Nothing.

---

## Set That Process Cares About CPU Tracking
Subscribes or unsubscribes the calling process to/from system rolling CPU usage metric tracking.

### Input
* `rdi` - `64`
* `rax` - `1` to enable CPU tracking subscription, `0` to disable subscription.

### Output
Nothing.
