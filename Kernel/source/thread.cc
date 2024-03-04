#include "thread.h"

#include "io.h"
#include "liballoc.h"
#include "object_pool.h"
#include "physical_allocator.h"
#include "process.h"
#include "registers.h"
#include "scheduler.h"
#include "text_terminal.h"
#include "virtual_allocator.h"

namespace {

// The model specific register that stores the FS segment's base address.
#define FSBASE_MSR 0xC0000100

// The number of stack pages.
#define STACK_PAGES 8

size_t next_thread_id;

// Initializes the registers for a thread.
void InitializeRegisters(const Process& process, size_t entry_point,
                         size_t param, Thread& thread) {
  Registers& registers = thread.registers;
  // We'll pass a parameter into 'rdi' (this can be used as a function
  // argument.)
  registers.rdi = param;

  // Sets the instruction pointer to our entry point.
  registers.rip = entry_point;

  // Sets the stack pointer and stack base to the top of our stack. (Stacks grow
  // down!)
  registers.rbp = registers.rsp = thread.stack + PAGE_SIZE * STACK_PAGES;

  // Sets our code and stack segment selectors (the segments are defined in
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
Thread* CreateThread(Process* process, size_t entry_point, size_t param) {
  Thread* thread = ObjectPool<Thread>::Allocate();
  if (thread == nullptr) return nullptr;

  thread->process = process;

  // Give this thread a unique ID. TODO: Make this a unique ID within the
  // process.
  thread->id = next_thread_id;
  next_thread_id++;

  // Sets up the stack by:
  // 1) Finds a free page in the process's virtual address space.
  thread->stack = AllocateVirtualMemoryInAddressSpace(
      &thread->process->virtual_address_space, STACK_PAGES);

  InitializeRegisters(*process, entry_point, param, *thread);

  // No thread segment.
  thread->thread_segment_offset = (size_t) nullptr;

  // The thread isn't initially awake until we schedule it.
  thread->awake = false;

  // The thread hasn't ran for any time slices yet.
  thread->time_slices = 0;

  // The thread isn't sleeping waiting for messages.
  thread->thread_is_waiting_for_message = false;

  // Add this to the linked list of threads in the process.
  process->threads.AddBack(thread);

  // Increment the process's thread cont.
  process->thread_count++;

  // Populate the FPU registers with something.
  memset(thread->fpu_registers, 0, 512);

  thread->address_to_clear_on_termination = 0;
  thread->uses_fpu_registers = true;

  return thread;
}

// Destroys a thread.
void DestroyThread(Thread* thread, bool process_being_destroyed) {
  // Make sure the thread is not scheduled.
  if (thread->awake) {
    UnscheduleThread(thread);
  }

  // Free the thread's stack.
  for (int i = 0; i < STACK_PAGES; i++, thread->stack += PAGE_SIZE) {
    UnmapVirtualPage(&thread->process->virtual_address_space, thread->stack,
                     true);
  }

  Process* process = thread->process;

  // If this thread is waiting for a message, remove it from the process's
  // queue of threads waiting for messages.
  if (thread->thread_is_waiting_for_message)
    process->threads_sleeping_for_message.Remove(thread);

  // Remove this thread from the process's linked list of threads.
  process->threads.Remove(thread);

  // The thread has a virtual address that should be cleared.
  if (thread->address_to_clear_on_termination) {
    // Find the virtual page and offset of the address.
    size_t offset_in_page =
        thread->address_to_clear_on_termination & (PAGE_SIZE - 1);
    size_t page = thread->address_to_clear_on_termination - offset_in_page;

    // Get the physical page.
    size_t physical_page = GetPhysicalAddress(
        &thread->process->virtual_address_space, offset_in_page,
        /*ignore_unowned_pages=*/false);
    if (physical_page != OUT_OF_MEMORY) {
      // If this virtual page was actually assigned to a physical address, set
      // our memory location to 0.
      *(uint64*)((size_t)TemporarilyMapPhysicalMemory(physical_page, 1) +
                 offset_in_page) = 0;
    }
  }

  // Free the thread object.
  ObjectPool<Thread>::Release(thread);

  // Decrease the thread count.
  process->thread_count--;

  // If no more threads are running (and we're not in the middle of destroying
  // it already), we can destroy it.
  if (process->thread_count == 0 && !process_being_destroyed) {
    DestroyProcess(process);
  }
}

// Destroys all threads for a process.
void DestroyThreadsForProcess(Process* process, bool process_being_destroyed) {
  while (Thread* thread = process->threads.FirstItem())
    DestroyThread(thread, process_being_destroyed);
}

// Returns a thread with the provided tid in process, return 0 if it doesn't
// exist.
Thread* GetThreadFromTid(Process* process, size_t tid) {
  for (Thread* thread : process->threads)
    if (thread->id == tid) return thread;
  return 0;
}

// Set the thread's segment offset (FS).
void SetThreadSegment(Thread* thread, size_t address) {
  thread->thread_segment_offset = address;

  if (thread == running_thread) {
    LoadThreadSegment(thread);
  }
}

// Load's a thread segment.
void LoadThreadSegment(Thread* thread) {
  WriteModelSpecificRegister(FSBASE_MSR, thread->thread_segment_offset);
}

#ifdef __TEST__
void "C" JumpIntoThread() {}
#endif
