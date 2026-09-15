#include "scheduling/thread.h"

#include "memory/heap_allocator.h"
#include "hardware/io.h"
#include "containers/object_pool.h"
#include "scheduling/cpu_core.h"
#include "memory/physical_allocator.h"
#include "processes/process.h"
#include "hardware/registers.h"
#include "scheduling/scheduler.h"
#include "containers/spinlock.h"
#include "output/text_terminal.h"
#include "memory/virtual_address_space.h"
#include "memory/virtual_allocator.h"
#include "ipc/shared_memory_manager.h"

namespace scheduling {


using containers::CpuPause;
using containers::InterruptSafeSpinlockGuard;
using containers::ObjectPool;
using hardware::AllocateFpuSaveArea;
using hardware::kFsBaseMsr;
using hardware::kKernelGsBaseMsr;
using hardware::Registers;
using hardware::ReleaseFpuSaveArea;
using hardware::WriteModelSpecificRegister;
using memory::kPageSize;
using memory::TemporarilyMapPhysicalPages;
using processes::AwakeFutexInProcess;
using processes::DestroyProcess;
using processes::Process;

namespace {

// The number of stack pages allocated per thread stack.
constexpr size_t kStackPages = 64;

size_t g_next_thread_id;

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

  // Sets the code and stack segment selectors.
  registers.cs = kUserCodeSelector | kUserRpl;
  registers.ss = kUserDataSelector | kUserRpl;

  // Sets up the processor's flags.
  registers.rflags =
      ((process.is_driver) ? ((1 << 12) | (1 << 13))
                           : 0) |  // Sets the IOPL bits for drivers.
      (1 << 9) |                   // Interrupts are enabled.
      (1 << 21);                   // The thread can use CPUID.
}

}  // namespace

void InitializeThreads() { g_next_thread_id = 0; }

void AcquireThreadReference(Thread& thread) {
  __atomic_add_fetch(&thread.reference_count, 1, __ATOMIC_RELAXED);
}

void ReleaseThreadReference(Thread& thread) {
  if (__atomic_sub_fetch(&thread.reference_count, 1, __ATOMIC_RELEASE) == 0) {
    ObjectPool<Thread>::Release(&thread);
  }
}

Thread* CreateThread(Process* process, size_t entry_point, size_t param,
                     size_t stack_pointer, size_t tls_base) {
  Thread* thread = ObjectPool<Thread>::Allocate();
  if (thread == nullptr) return nullptr;

  thread->reference_count = 1;
  thread->process = process;

  // Give this thread a unique ID. TODO: Make this a unique ID within the
  // process.
  thread->id = __atomic_fetch_add(&g_next_thread_id, 1, __ATOMIC_RELAXED);

  // Sets up the stack.
  if (stack_pointer == 0) {
    thread->stack =
        thread->process->virtual_address_space.AllocatePages(kStackPages);
    if (thread->stack == kOutOfMemory) {
      ObjectPool<Thread>::Release(thread);
      return nullptr;
    }
    thread->stack_allocated_by_kernel = true;
    stack_pointer = thread->stack + kPageSize * kStackPages;
  } else {
    thread->stack = 0;
    thread->stack_allocated_by_kernel = false;
  }

  // Make the stack pointer 16-byte aligned.
  size_t adjusted_stack_pointer = stack_pointer & ~15UL;
  // Function entry points expect for non-primary threads to be stack aligned at
  // 16 bytes - 8 bytes. This simulates how the return address in the call stack
  // is right above it.
  if (process->threads.count() > 0) adjusted_stack_pointer -= 8;

  InitializeRegisters(*process, entry_point, param, adjusted_stack_pointer,
                      *thread);

  // Set the TLS bases.
  thread->thread_fs_segment_offset = tls_base;
  thread->thread_gs_segment_offset = (size_t)nullptr;

  // The thread isn't initially awake until scheduled.
  thread->TransitionToHalted();
  thread->wake_signal_pending = false;

  // The thread hasn't run for any time slices yet.
  thread->time_slices = 0;
  thread->remaining_timeslice_microseconds = kDefaultTimesliceMicroseconds;
  thread->current_run_start_timestamp = 0;
  thread->running_on_core = -1;
  thread->thread_is_waiting_for_shared_memory = nullptr;

  // Initialize thread priority on spawn.
  if (process->threads.count() == 0) {
    thread->priority = process->is_driver ? ThreadPriority::InterruptDriver
                                          : ThreadPriority::Normal;
  } else {
    thread->priority = ThreadPriority::Background;
  }

  // Add this to the tree of threads in the process.
  {
    InterruptSafeSpinlockGuard guard(process->lock);
    process->threads.Insert(thread);
  }

  // Populate the FPU registers with something.
  thread->fpu_registers = AllocateFpuSaveArea();

  thread->address_to_clear_on_termination = 0;
  thread->uses_fpu_registers = (thread->fpu_registers != nullptr);
  thread->in_syscall = false;

  return thread;
}

// Destroys a thread.
void DestroyThread(Thread* thread, bool process_being_destroyed) {
  // Make sure the thread is not scheduled.
  if (thread->is_awake()) UnscheduleThread(thread, ThreadState::Terminated);

  // Wait if the thread is currently running on another core until it
  // deschedules.
  if (thread != RunningThread()) {
    while (__atomic_load_n(&thread->running_on_core, __ATOMIC_ACQUIRE) != -1)
      CpuPause();
  }

  // Free the thread's stack.
  if (thread->stack_allocated_by_kernel) {
    thread->process->virtual_address_space.FreePages(thread->stack,
                                                     kStackPages);
  }

  Process* process = thread->process;

  // If this thread is waiting for a message, remove it from the process's
  // queue of threads waiting for messages.
  {
    InterruptSafeSpinlockGuard guard(process->message_lock);
    if (thread->is_waiting_for_message()) {
      process->threads_sleeping_for_message.Remove(thread);
      thread->TransitionToTerminated();
    }
  }

  // If this thread is waiting for a shared memory page, unlink it.
  if (thread->is_waiting_for_shared_memory()) {
    ipc::SharedMemoryManager::Get().RemoveWaitingThread(*thread);
    thread->TransitionToTerminated();
  }

  // Remove this thread from the process's tree of threads.
  bool should_destroy_process = false;
  {
    InterruptSafeSpinlockGuard guard(process->lock);
    process->threads.Remove(thread);
    if (process->threads.IsEmpty() && !process_being_destroyed)
      should_destroy_process = true;
  }

  // The thread has a virtual address that should be cleared.
  if (thread->address_to_clear_on_termination) {
    size_t address_cleared = thread->address_to_clear_on_termination;
    // Find the virtual page and offset of the address.
    size_t offset_in_page = address_cleared & (kPageSize - 1);
    size_t page = address_cleared - offset_in_page;

    // Get the physical page.
    size_t physical_page =
        thread->process->virtual_address_space.GetPhysicalAddress(
            page,
            /*ignore_unowned_pages=*/false);
    if (physical_page != kOutOfMemory) {
      // If this virtual page was actually assigned to a physical address,
      // set the memory location to 0.
      *(uint64*)((size_t)TemporarilyMapPhysicalPages(physical_page, 1) +
                 offset_in_page) = 0;

      AwakeFutexInProcess(process, address_cleared);
    }
  }

  if (thread->fpu_registers) {
    ReleaseFpuSaveArea(thread->fpu_registers);
    thread->fpu_registers = nullptr;
  }

  // Free the thread object when all references have been released.
  ReleaseThreadReference(*thread);

  // If no more threads are running (and not in the middle of destroying
  // it already), destroy it.
  if (should_destroy_process) DestroyProcess(process);
}


// Destroys all threads for a process.
void DestroyThreadsForProcess(Process* process, bool process_being_destroyed) {
  while (true) {
    Thread* thread = nullptr;
    {
      InterruptSafeSpinlockGuard guard(process->lock);
      thread = process->threads.FirstItem();
    }
    if (thread == nullptr) break;
    DestroyThread(thread, process_being_destroyed);
  }
}

Thread* GetThreadFromTid(Process* process, size_t tid) {
  InterruptSafeSpinlockGuard guard(process->lock);
  return process->threads.SearchForItemEqualToValue(tid);
}

void Thread::TransitionToReady() {
  state = ThreadState::Ready;
  thread_is_waiting_for_shared_memory = nullptr;
}

void Thread::TransitionToRunning(int core_id) {
  state = ThreadState::Running;
  __atomic_store_n(&running_on_core, core_id, __ATOMIC_RELEASE);
  thread_is_waiting_for_shared_memory = nullptr;
}

void Thread::TransitionToBlockedOnMessage() {
  state = ThreadState::BlockedOnMessage;
}

void Thread::TransitionToBlockedOnMemory(
    ipc::ThreadWaitingForSharedMemoryPage* wait_record) {
  state = ThreadState::BlockedOnMemory;
  thread_is_waiting_for_shared_memory = wait_record;
}

void Thread::TransitionToHalted() {
  state = ThreadState::Halted;
}

void Thread::TransitionToTerminated() {
  state = ThreadState::Terminated;
  thread_is_waiting_for_shared_memory = nullptr;
}


void Thread::SetSegment(size_t address) {
  thread_fs_segment_offset = address;
  if (RunningThread() != nullptr && this == RunningThread()) LoadSegment();
}

void Thread::SetSegments(size_t fs_address, bool set_fs, size_t gs_address,
                         bool set_gs) {
  if (set_fs) thread_fs_segment_offset = fs_address;
  if (set_gs) thread_gs_segment_offset = gs_address;
  if (RunningThread() != nullptr && this == RunningThread()) LoadSegment();
}

void Thread::LoadSegment() {
#ifndef TEST
  CpuCoreState& cpu = GetCurrentCpuCore();
  if (cpu.cached_fs_base != thread_fs_segment_offset) {
    WriteModelSpecificRegister(kFsBaseMsr, thread_fs_segment_offset);
    cpu.cached_fs_base = thread_fs_segment_offset;
  }
  if (cpu.cached_user_gs_base != thread_gs_segment_offset) {
    WriteModelSpecificRegister(kKernelGsBaseMsr, thread_gs_segment_offset);
    cpu.cached_user_gs_base = thread_gs_segment_offset;
  }
#endif
}

void SetThreadSegment(Thread* thread, size_t address) {
  thread->SetSegment(address);
}

void SetThreadSegments(Thread* thread, size_t fs_address, bool set_fs,
                       size_t gs_address, bool set_gs) {
  thread->SetSegments(fs_address, set_fs, gs_address, set_gs);
}

void LoadThreadSegment(Thread* thread) {
  thread->LoadSegment();
}

}  // namespace scheduling

#ifdef TEST
extern "C" void JumpIntoThread() {}
#endif
