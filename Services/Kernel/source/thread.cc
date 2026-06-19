#include "thread.h"

#include "io.h"
#include "heap_allocator.h"
#include "object_pool.h"
#include "physical_allocator.h"
#include "process.h"
#include "registers.h"
#include "scheduler.h"
#include "text_terminal.h"
#include "virtual_address_space.h"
#include "virtual_allocator.h"

namespace {

// The model specific register that stores the FS segment's base address.
#define FSBASE_MSR 0xC0000100

// The model specific register that stores the GS segment's base address.
#define GSBASE_MSR 0xC0000101

// The number of stack pages.
#define STACK_PAGES 64

size_t next_thread_id;

// Initializes the registers for a thread.
void InitializeRegisters(const Process& process, size_t entry_point,
                         size_t param, size_t stack_pointer, Thread& thread) {
  Registers& registers = thread.registers;
  // A parameter will be passed into 'rdi' (this can be used as a function
  // argument.)
  registers.rdi = param;

  // Sets the instruction pointer to the entry point.
  registers.rip = entry_point;

  // Sets the stack pointer and stack base to the top of the stack. (Stacks grow
  // down!)
  registers.rbp = registers.rsp = stack_pointer;

  // Sets the code and stack segment selectors (the segments are defined in
  // Gdt64 in boot.asm)
  registers.cs =
      0x20 | 3;  // '| 3' means ring 3. This is a user code, not kernel code.
  registers.ss = 0x18 | 3;  // Likewise with user data, not kernel data.

  // Sets up the processor's flags.
  registers.rflags =
      ((process.is_driver) ? ((1 << 12) | (1 << 13))
                           : 0) |  // Sets the IOPL bits for drivers.
      (1 << 9) |                   // Interrupts are enabled.
      (1 << 21);                   // The thread can use CPUID.
}

}  // namespace

extern void save_fpu_registers(size_t regs_addr);

// Initialize threads.
void InitializeThreads() {
  // Clears our linked list.
  next_thread_id = 0;
}

// Createss a thread.
Thread* CreateThread(Process* process, size_t entry_point, size_t param,
                     size_t stack_pointer, size_t tls_base) {
  Thread* thread = ObjectPool<Thread>::Allocate();
  if (thread == nullptr) return nullptr;

  thread->process = process;

  // Give this thread a unique ID. TODO: Make this a unique ID within the
  // process.
  thread->id = next_thread_id;
  next_thread_id++;

  // Sets up the stack.
  if (stack_pointer == 0) {
    thread->stack =
        thread->process->virtual_address_space.AllocatePages(STACK_PAGES);
    thread->stack_allocated_by_kernel = true;
    stack_pointer = thread->stack + PAGE_SIZE * STACK_PAGES;
  } else {
    thread->stack = 0;
    thread->stack_allocated_by_kernel = false;
  }

  // Make the satck pointer null-aligned.
  size_t adjusted_stack_pointer = stack_pointer & ~15UL;
  // Function entry points expect for non-primary threads to be stack aligned at
  // 16 bytes - 8 bytes. This simulates how the return address in the call stack
  // is right above it.
  if (process->thread_count > 0) adjusted_stack_pointer -= 8;

  InitializeRegisters(*process, entry_point, param, adjusted_stack_pointer,
                      *thread);

  // Set the TLS bases.
  thread->thread_fs_segment_offset = tls_base;
  thread->thread_gs_segment_offset = (size_t)nullptr;

  // The thread isn't initially awake until we schedule it.
  thread->awake = false;
  thread->wake_signal_pending = false;

  // The thread hasn't ran for any time slices yet.
  thread->time_slices = 0;
  thread->remaining_timeslice_microseconds = 10000;
  thread->current_run_start_timestamp = 0;

  // Initialize thread priority on spawn.
  if (process->thread_count == 0) {
    thread->priority = process->is_driver ? ThreadPriority::InterruptDriver
                                          : ThreadPriority::Normal;
  } else {
    thread->priority = ThreadPriority::Background;
  }

  // The thread isn't sleeping waiting for messages.
  thread->thread_is_waiting_for_message = false;

  // Add this to the tree of threads in the process.
  process->threads.Insert(thread);

  // Increment the process's thread cont.
  process->thread_count++;

  // Populate the FPU registers with something.
  memset(thread->fpu_registers, 0, 512);

  thread->address_to_clear_on_termination = 0;
  thread->uses_fpu_registers = true;
  thread->in_syscall = false;

  return thread;
}

// Destroys a thread.
void DestroyThread(Thread* thread, bool process_being_destroyed) {
  // Make sure the thread is not scheduled.
  if (thread->awake) {
    UnscheduleThread(thread);
  }

  // Free the thread's stack.
  if (thread->stack_allocated_by_kernel) {
    thread->process->virtual_address_space.FreePages(thread->stack,
                                                     STACK_PAGES);
  }

  Process* process = thread->process;

  // If this thread is waiting for a message, remove it from the process's
  // queue of threads waiting for messages.
  if (thread->thread_is_waiting_for_message)
    process->threads_sleeping_for_message.Remove(thread);

  // Remove this thread from the process's tree of threads.
  process->threads.Remove(thread);

  // The thread has a virtual address that should be cleared.
  if (thread->address_to_clear_on_termination) {
    // Find the virtual page and offset of the address.
    size_t offset_in_page =
        thread->address_to_clear_on_termination & (PAGE_SIZE - 1);
    size_t page = thread->address_to_clear_on_termination - offset_in_page;

    // Get the physical page.
    size_t physical_page =
        thread->process->virtual_address_space.GetPhysicalAddress(
            page,
            /*ignore_unowned_pages=*/false);
    if (physical_page != OUT_OF_MEMORY) {
      // If this virtual page was actually assigned to a physical address,
      // set the memory location to 0.
      *(uint64*)((size_t)TemporarilyMapPhysicalPages(physical_page, 1) +
                 offset_in_page) = 0;
    }
  }

  // Free the thread object.
  ObjectPool<Thread>::Release(thread);

  // Decrease the thread count.
  process->thread_count--;

  // If no more threads are running (and not in the middle of destroying
  // it already), destroy it.
  if (process->thread_count == 0 && !process_being_destroyed) {
    DestroyProcess(process);
  }
}

// Destroys all threads for a process.
void DestroyThreadsForProcess(Process* process, bool process_being_destroyed) {
  while (Thread* thread = process->threads.FirstItem())
    DestroyThread(thread, process_being_destroyed);
}

Thread* GetThreadFromTid(Process* process, size_t tid) {
  return process->threads.SearchForItemEqualToValue(tid);
}

// Set the thread's segment offset (FS).
void SetThreadSegment(Thread* thread, size_t address) {
  thread->thread_fs_segment_offset = address;

  if (running_thread != nullptr && thread == running_thread) {
    LoadThreadSegment(thread);
  }
}

// Set the thread's segments (FS and/or GS).
void SetThreadSegments(Thread* thread, size_t fs_address, bool set_fs,
                       size_t gs_address, bool set_gs) {
  if (set_fs) thread->thread_fs_segment_offset = fs_address;
  if (set_gs) thread->thread_gs_segment_offset = gs_address;

  if (running_thread != nullptr && thread == running_thread) {
    LoadThreadSegment(thread);
  }
}

// Load's a thread segment.
void LoadThreadSegment(Thread* thread) {
  WriteModelSpecificRegister(FSBASE_MSR, thread->thread_fs_segment_offset);
  WriteModelSpecificRegister(GSBASE_MSR, thread->thread_gs_segment_offset);
}

#ifdef TEST
extern "C" void JumpIntoThread() {}
#endif
