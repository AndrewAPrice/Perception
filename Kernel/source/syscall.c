#include "syscall.h"

#include "exceptions.h"
#include "framebuffer.h"
#include "interrupts.h"
#include "io.h"
#include "messages.h"
#include "physical_allocator.h"
#include "process.h"
#include "profiling.h"
#include "registers.h"
#include "scheduler.h"
#include "service.h"
#include "shared_memory.h"
#include "text_terminal.h"
#include "thread.h"
#include "timer.h"
#include "virtual_allocator.h"

// Uncomment for debug printing.
// #define DEBUG

extern void syscall_entry();

// MSR that contains the kernel's SYSCALL entrypoint.
#define LSTAR 0xC0000082

#define STAR 0xC0000081
// Kernel segment CS is as is, and DS is +8.
#define KERNEL_SEGMENT_BASE (0x08L << 32L)
// User segment CS is +16, and DS is +8.
#define USER_SEGMENT_BASE (0x10L << 48L)

// MSR that contains the RFLAGS mask during system calls.
#define IA32_FMASK 0xC0000084
// Mask for the interrupt bit in IA32_FMASK
#define INTERRUPT_MASK 0x0200

void InitializeSystemCalls() {
#ifndef __TEST__
  wrmsr(STAR, KERNEL_SEGMENT_BASE | USER_SEGMENT_BASE);
  wrmsr(LSTAR, (size_t)syscall_entry);
  // Disable interrupts duing syscalls.
  wrmsr(IA32_FMASK, INTERRUPT_MASK);
//  SetInterruptHandler(0x80, (size_t)syscall_isr, 0x08, 0x8E);
#endif
}

// Syscalls.
#define PRINT_DEBUG_CHARACTER 0
#define PRINT_REGISTERS_AND_STACK 26
// Threading
#define CREATE_THREAD 1
#define GET_THIS_THREAD_ID 2
#define SLEEP_THIS_THREAD 3
#define SLEEP_THREAD 9
#define WAKE_THREAD 10
#define WAKE_AND_SWITCH_TO_THREAD 11
#define TERMINATE_THIS_THREAD 4
#define TERMINATE_THREAD 5
#define YIELD 8
#define SET_THREAD_SEGMENT 27
#define SET_ADDRESS_TO_CLEAR_ON_THREAD_TERMINATION 28
// Memory management
#define ALLOCATE_MEMORY_PAGES 12
#define ALLOCATE_MEMORY_PAGES_BELOW_PHYSICAL_BASE 49
#define RELEASE_MEMORY_PAGES 13
#define MAP_PHYSICAL_MEMORY 41
#define GET_PHYSICAL_ADDRESS_OF_VIRTUAL_ADDRESS 50
#define GET_FREE_SYSTEM_MEMORY 14
#define GET_MEMORY_USED_BY_PROCESS 15
#define GET_TOTAL_SYSTEM_MEMORY 16
#define CREATE_SHARED_MEMORY 42
#define JOIN_SHARED_MEMORY 43
#define LEAVE_SHARED_MEMORY 44
#define MOVE_PAGE_INTO_SHARED_MEMORY 45
#define IS_SHARED_MEMORY_PAGE_ALLOCATED 46
#define SET_MEMORY_ACCESS_RIGHTS 48
// Processes
#define GET_THIS_PROCESS_ID 39
#define TERMINATE_THIS_PROCESS 6
#define TERMINATE_PROCESS 7
#define GET_PROCESSES 22
#define GET_NAME_OF_PROCESS 29
#define NOTIFY_WHEN_PROCESS_DISAPPEARS 30
#define STOP_NOTIFYING_WHEN_PROCESS_DISAPPEARS 31
// Services
#define REGISTER_SERVICE 32
#define UNREGISTER_SERVICE 33
#define GET_SERVICES 34
#define GET_NAME_OF_SERVICE 47
#define NOTIFY_WHEN_SERVICE_APPEARS 35
#define STOP_NOTIFYING_WHEN_SERVICE_APPEARS 36
#define NOTIFY_WHEN_SERVICE_DISAPPEARS 37
#define STOP_NOTIFYING_WHEN_SERVICE_DISAPPEARS 38
// Messaging
#define SEND_MESSAGE 17
#define POLL_FOR_MESSAGE 18
#define SLEEP_FOR_MESSAGE 19
// Interrupts
#define REGISTER_MESSAGE_TO_SEND_ON_INTERRUPT 20
#define UNREGISTER_MESSAGE_TO_SEND_ON_INTERRUPT 21
// Drivers
#define GET_MULTIBOOT_FRAMEBUFFER_INFORMATION 40
// Time
#define SEND_MESSAGE_AFTER_X_MICROSECONDS 23
#define SEND_MESSAGE_AT_TIMESTAMP 24
#define GET_CURRENT_TIMESTAMP 25

extern void JumpIntoThread();

void SyscallHandler(int syscall_number) {
#ifdef DEBUG
  PrintString("Entering syscall ");
  PrintString(GetSystemCallName(syscall));
  PrintString(" (") PrintNumber(syscall_number);
  PrintString(" )\n");
  PrintRegisters(currently_executing_thread_regs);
#endif

#ifdef PROFILING_ENABLED
  size_t syscall_start_time = CurrentTimeForProfiling();
#endif
  switch (syscall_number) {
    case PRINT_DEBUG_CHARACTER:
      PrintChar((unsigned char)currently_executing_thread_regs->rax);
      break;
    case PRINT_REGISTERS_AND_STACK:
      PrintString("Dump requested by PID ");
      struct Process *process = running_thread->process;
      PrintNumber(running_thread->process->pid);
      PrintString(" (");
      PrintString(running_thread->process->name);
      PrintString(") in TID ");
      PrintNumber(running_thread->id);
      PrintChar('\n');

      PrintRegistersAndStackTrace();
      break;
    case CREATE_THREAD: {
      struct Thread *new_thread = CreateThread(
          running_thread->process, currently_executing_thread_regs->rax,
          currently_executing_thread_regs->rbx);
      if (new_thread == 0) {
        currently_executing_thread_regs->rax = 0;
      } else {
        currently_executing_thread_regs->rax = new_thread->id;
      }
      ScheduleThread(new_thread);
      break;
    }
    case GET_THIS_THREAD_ID:
      currently_executing_thread_regs->rax = running_thread->id;
      break;
    case SLEEP_THIS_THREAD:
      PrintString("Implement SLEEP_THREAD\n");
      break;
    case SLEEP_THREAD:
      PrintString("Implement SLEEP\n");
      break;
    case WAKE_THREAD:
      // TODO: if thread is waiting for event, set bad event id
      break;
    case WAKE_AND_SWITCH_TO_THREAD:
      // TODO: if thread is waiting for event, set bad event id
      break;
    case TERMINATE_THIS_THREAD:
      DestroyThread(running_thread, false);
      JumpIntoThread();  // Doesn't return.
      break;
    case TERMINATE_THREAD: {
      struct Thread *thread = GetThreadFromTid(
          running_thread->process, currently_executing_thread_regs->rax);
      if (thread == running_thread) {
        DestroyThread(running_thread, false);
        JumpIntoThread();  // Doesn't return.
      } else if (thread != 0) {
        DestroyThread(thread, false);
      }
      break;
    }
    case YIELD:
      ScheduleNextThread();
      JumpIntoThread();  // Doesn't return.
      break;
    case SET_THREAD_SEGMENT:
      SetThreadSegment(running_thread, currently_executing_thread_regs->rax);
      break;
    case SET_ADDRESS_TO_CLEAR_ON_THREAD_TERMINATION:
      // Align the address to 8 bytes to avoid crossing page boundaries.
      running_thread->address_to_clear_on_termination =
          currently_executing_thread_regs->rax & (~7L);
      break;
    case ALLOCATE_MEMORY_PAGES:
      currently_executing_thread_regs->rax =
          AllocateVirtualMemoryInAddressSpace(
              &running_thread->process->virtual_address_space,
              currently_executing_thread_regs->rax);
      break;
    case ALLOCATE_MEMORY_PAGES_BELOW_PHYSICAL_BASE:
      if (running_thread->process->is_driver) {
        size_t first_physical_address = 0;
        currently_executing_thread_regs->rax =
            AllocateVirtualMemoryInAddressSpaceBelowMaxBaseAddress(
                &running_thread->process->virtual_address_space,
                currently_executing_thread_regs->rax,
                currently_executing_thread_regs->rbx);
        currently_executing_thread_regs->rbx =
            GetPhysicalAddress(&running_thread->process->virtual_address_space,
                               currently_executing_thread_regs->rax,
                               /*ignore_unowned_pages=*/false);
      } else {
        currently_executing_thread_regs->rax = OUT_OF_MEMORY;
        currently_executing_thread_regs->rbx = 0;
      }
      break;
    case RELEASE_MEMORY_PAGES:
      ReleaseVirtualMemoryInAddressSpace(
          &running_thread->process->virtual_address_space,
          currently_executing_thread_regs->rax,
          currently_executing_thread_regs->rbx, true);
      break;
    case MAP_PHYSICAL_MEMORY:
      // Only drivers can map physical memory.
      if (running_thread->process->is_driver) {
        currently_executing_thread_regs->rax = MapPhysicalMemoryInAddressSpace(
            &running_thread->process->virtual_address_space,
            currently_executing_thread_regs->rax,
            currently_executing_thread_regs->rbx);
      } else {
        currently_executing_thread_regs->rax = OUT_OF_MEMORY;
      }
      break;
    case GET_PHYSICAL_ADDRESS_OF_VIRTUAL_ADDRESS:
      if (running_thread->process->is_driver) {
        currently_executing_thread_regs->rax =
            GetPhysicalAddress(&running_thread->process->virtual_address_space,
                               currently_executing_thread_regs->rax,
                               /*ignore_unowned_pages=*/false);
      } else {
        currently_executing_thread_regs->rax = 0;
      }
      break;
    case GET_FREE_SYSTEM_MEMORY:
      currently_executing_thread_regs->rax = free_pages * PAGE_SIZE;
      break;
    case GET_MEMORY_USED_BY_PROCESS:
      currently_executing_thread_regs->rax =
          running_thread->process->allocated_pages * PAGE_SIZE;
      break;
    case GET_TOTAL_SYSTEM_MEMORY:
      currently_executing_thread_regs->rax = total_system_memory;
      break;
    case CREATE_SHARED_MEMORY: {
      struct SharedMemoryInProcess *shared_memory =
          CreateAndMapSharedMemoryBlockIntoProcess(
              running_thread->process, currently_executing_thread_regs->rax,
              currently_executing_thread_regs->rbx,
              currently_executing_thread_regs->rdx);
      if (shared_memory == NULL) {
        // Could not create the shared memory block.
        currently_executing_thread_regs->rax = 0;
        currently_executing_thread_regs->rbx = 0;
      } else {
        // Created the shared memory block.
        currently_executing_thread_regs->rax = shared_memory->shared_memory->id;
        currently_executing_thread_regs->rbx = shared_memory->virtual_address;
      }
      break;
    }
    case JOIN_SHARED_MEMORY: {
      struct SharedMemoryInProcess *shared_memory = JoinSharedMemory(
          running_thread->process, currently_executing_thread_regs->rax);

      if (shared_memory == NULL) {
        // Could not join the shared memory block.
        currently_executing_thread_regs->rax = 0;
        currently_executing_thread_regs->rbx = 0;
        currently_executing_thread_regs->rdx = 0;
      } else {
        // Joined the shared memory block.
        currently_executing_thread_regs->rax =
            shared_memory->shared_memory->size_in_pages;
        currently_executing_thread_regs->rbx = shared_memory->virtual_address;
        currently_executing_thread_regs->rdx =
            shared_memory->shared_memory->flags;
      }
      break;
    }
    case LEAVE_SHARED_MEMORY:
      LeaveSharedMemory(running_thread->process,
                        currently_executing_thread_regs->rax);
      break;
    case MOVE_PAGE_INTO_SHARED_MEMORY:
      MovePageIntoSharedMemory(running_thread->process,
                               currently_executing_thread_regs->rax,
                               currently_executing_thread_regs->rbx,
                               currently_executing_thread_regs->rdx);
      break;
    case IS_SHARED_MEMORY_PAGE_ALLOCATED:
      currently_executing_thread_regs->rax =
          IsAddressAllocatedInSharedMemory(currently_executing_thread_regs->rax,
                                           currently_executing_thread_regs->rbx)
              ? 1
              : 0;
      break;
    case SET_MEMORY_ACCESS_RIGHTS: {
      size_t address = currently_executing_thread_regs->rax;
      size_t num_pages = currently_executing_thread_regs->rbx;
      size_t max_address = address + num_pages * PAGE_SIZE;
      size_t rights = currently_executing_thread_regs->rdx;

#ifdef DEBUG
      PrintString(running_thread->process->name);
      PrintString(" protecting ");
      PrintNumber(num_pages);
      PrintString(" page(s) from ");
      PrintHex(address);
      PrintString(" to ");
      PrintHex(max_address);
      PrintString(" with rights ");
      PrintNumber(rights);
      PrintChar('\n');
#endif

      for (; address < max_address; address += PAGE_SIZE) {
        SetMemoryAccessRights(&running_thread->process->virtual_address_space,
                              address, rights);
      }
      break;
    }
    case GET_THIS_PROCESS_ID:
      currently_executing_thread_regs->rax = running_thread->process->pid;
      break;
    case TERMINATE_THIS_PROCESS:
      DestroyProcess(running_thread->process);
      JumpIntoThread();  // Doesn't return.
      break;
    case TERMINATE_PROCESS: {
      struct Process *process =
          GetProcessFromPid(currently_executing_thread_regs->rax);
      if (process == NULL) {
        break;
      }
      bool currently_running_process = process == running_thread->process;
      DestroyProcess(process);
      if (currently_running_process) {
        JumpIntoThread();  // Doesn't return.
      }
      break;
    }
    case GET_PROCESSES: {
      // Extract the name from the input registers.
      size_t process_name[PROCESS_NAME_WORDS];
      process_name[0] = currently_executing_thread_regs->rax;
      process_name[1] = currently_executing_thread_regs->rbx;
      process_name[2] = currently_executing_thread_regs->rdx;
      process_name[3] = currently_executing_thread_regs->rsi;
      process_name[4] = currently_executing_thread_regs->r8;
      process_name[5] = currently_executing_thread_regs->r9;
      process_name[6] = currently_executing_thread_regs->r10;
      process_name[7] = currently_executing_thread_regs->r12;
      process_name[8] = currently_executing_thread_regs->r13;
      process_name[9] = currently_executing_thread_regs->r14;
      process_name[10] = currently_executing_thread_regs->r15;

      // Loop over all processes starting from the provided PID
      // until we run out of processes. We will keep track of
      // the pids of the first 12 that we find.
      size_t pids[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
      size_t processes_found = 0;
      struct Process *process =
          GetProcessOrNextFromPid(currently_executing_thread_regs->rbp);
      while (process != NULL) {
        process = FindNextProcessWithName((char *)process_name, process);
        if (process != NULL) {
          if (processes_found < 12) pids[processes_found] = process->pid;

          processes_found++;
          process = process->next;
        }
      }

      // Write out the list of found PIDs.
      currently_executing_thread_regs->rdi = processes_found;
      currently_executing_thread_regs->rbp = pids[0];
      currently_executing_thread_regs->rax = pids[1];
      currently_executing_thread_regs->rbx = pids[2];
      currently_executing_thread_regs->rdx = pids[3];
      currently_executing_thread_regs->rsi = pids[4];
      currently_executing_thread_regs->r8 = pids[5];
      currently_executing_thread_regs->r9 = pids[6];
      currently_executing_thread_regs->r10 = pids[7];
      currently_executing_thread_regs->r12 = pids[8];
      currently_executing_thread_regs->r13 = pids[9];
      currently_executing_thread_regs->r14 = pids[10];
      currently_executing_thread_regs->r15 = pids[11];
      break;
    }
    case GET_NAME_OF_PROCESS: {
      struct Process *process =
          GetProcessFromPid(currently_executing_thread_regs->rax);
      if (process == NULL) {
        currently_executing_thread_regs->rdi = 0;
      } else {
        currently_executing_thread_regs->rdi = 1;
        currently_executing_thread_regs->rax = ((size_t *)process->name)[0];
        currently_executing_thread_regs->rbx = ((size_t *)process->name)[1];
        currently_executing_thread_regs->rdx = ((size_t *)process->name)[2];
        currently_executing_thread_regs->rsi = ((size_t *)process->name)[3];
        currently_executing_thread_regs->r8 = ((size_t *)process->name)[4];
        currently_executing_thread_regs->r9 = ((size_t *)process->name)[5];
        currently_executing_thread_regs->r10 = ((size_t *)process->name)[6];
        currently_executing_thread_regs->r12 = ((size_t *)process->name)[7];
        currently_executing_thread_regs->r13 = ((size_t *)process->name)[8];
        currently_executing_thread_regs->r14 = ((size_t *)process->name)[9];
        currently_executing_thread_regs->r15 = ((size_t *)process->name)[10];
      }
      break;
    }
    case NOTIFY_WHEN_PROCESS_DISAPPEARS: {
      size_t target_pid = currently_executing_thread_regs->rax;
      size_t event_id = currently_executing_thread_regs->rbx;

      struct Process *target =
          GetProcessFromPid(currently_executing_thread_regs->rax);
      if (target == NULL) {
        // The target process we want to be notified of when it dies
        // doesn't exist. It's possible that is just died, so whatever
        // the case, the safest thing to do here is to imemdiately send
        // an event.
        PrintString("NOTIFY_WHEN_PROCESS_DISAPPEARS");
        SendKernelMessageToProcess(running_thread->process, event_id,
                                   target_pid, 0, 0, 0, 0);
      } else {
        NotifyProcessOnDeath(target, running_thread->process, event_id);
      }
      break;
    }
    case STOP_NOTIFYING_WHEN_PROCESS_DISAPPEARS:
      PrintString("Implement STOP_NOTIFYING_WHEN_PROCESS_DISAPPEARS\n");
      break;
    case REGISTER_SERVICE: {
      // Extract the name from the input registers.
      size_t service_name[SERVICE_NAME_WORDS];
      service_name[0] = currently_executing_thread_regs->rax;
      service_name[1] = currently_executing_thread_regs->rbx;
      service_name[2] = currently_executing_thread_regs->rdx;
      service_name[3] = currently_executing_thread_regs->rsi;
      service_name[4] = currently_executing_thread_regs->r8;
      service_name[5] = currently_executing_thread_regs->r9;
      service_name[6] = currently_executing_thread_regs->r10;
      service_name[7] = currently_executing_thread_regs->r12;
      service_name[8] = currently_executing_thread_regs->r13;
      service_name[9] = currently_executing_thread_regs->r14;

#ifdef DEBUG
      PrintString("Registering service ");
      PrintString((char *)service_name);
      PrintString(" / ");
      PrintNumber(currently_executing_thread_regs->rbp);
      PrintString(" in process ");
      PrintNumber(running_thread->process->pid);
      PrintString("\n");
#endif

      RegisterService((char *)service_name, running_thread->process,
                      currently_executing_thread_regs->rbp);
      break;
    }
    case UNREGISTER_SERVICE:
      UnregisterServiceByMessageId(running_thread->process,
                                   currently_executing_thread_regs->rax);
      break;
    case GET_SERVICES: {
      // Extract the name from the input registers.
      size_t service_name[SERVICE_NAME_WORDS];
      service_name[0] = currently_executing_thread_regs->rbx;
      service_name[1] = currently_executing_thread_regs->rdx;
      service_name[2] = currently_executing_thread_regs->rsi;
      service_name[3] = currently_executing_thread_regs->r8;
      service_name[4] = currently_executing_thread_regs->r9;
      service_name[5] = currently_executing_thread_regs->r10;
      service_name[6] = currently_executing_thread_regs->r12;
      service_name[7] = currently_executing_thread_regs->r13;
      service_name[8] = currently_executing_thread_regs->r14;
      service_name[9] = currently_executing_thread_regs->r15;

      size_t min_pid = currently_executing_thread_regs->rbp;
      size_t min_sid = currently_executing_thread_regs->rax;

      // Loop over all processes starting from the provided PID
      // and services starting from the provided SID until we
      // run out of processes. We will keep track of the pids and
      // sids of the first 6 that we find.
      size_t pids[6] = {0, 0, 0, 0, 0, 0};
      size_t sids[6] = {0, 0, 0, 0, 0, 0};
      size_t services_found = 0;
      for (struct Service *service = FindNextServiceByPidAndMidWithName(
               (char *)service_name, min_pid, min_sid);
           service != NULL;
           service = FindNextServiceWithName((char *)service_name, service)) {
        if (services_found < 6) {
          pids[services_found] = service->process->pid;
          sids[services_found] = service->message_id;
        }
        services_found++;
      }
      // Write out the list of found PIDs.
      currently_executing_thread_regs->rdi = services_found;
      currently_executing_thread_regs->rbp = pids[0];
      currently_executing_thread_regs->rax = sids[0];
      currently_executing_thread_regs->rbx = pids[1];
      currently_executing_thread_regs->rdx = sids[1];
      currently_executing_thread_regs->rsi = pids[2];
      currently_executing_thread_regs->r8 = sids[2];
      currently_executing_thread_regs->r9 = pids[3];
      currently_executing_thread_regs->r10 = sids[3];
      currently_executing_thread_regs->r12 = pids[4];
      currently_executing_thread_regs->r13 = sids[4];
      currently_executing_thread_regs->r14 = pids[5];
      currently_executing_thread_regs->r15 = sids[5];
      break;
    }
    case GET_NAME_OF_SERVICE: {
      size_t pid = currently_executing_thread_regs->rax;
      size_t sid = currently_executing_thread_regs->rbx;
      struct Service *service = FindServiceByProcessAndMid(pid, sid);
      if (service == NULL) {
        currently_executing_thread_regs->rdi = 0;
      } else {
        currently_executing_thread_regs->rdi = 1;
        currently_executing_thread_regs->rax = ((size_t *)service->name)[0];
        currently_executing_thread_regs->rbx = ((size_t *)service->name)[1];
        currently_executing_thread_regs->rdx = ((size_t *)service->name)[2];
        currently_executing_thread_regs->rsi = ((size_t *)service->name)[3];
        currently_executing_thread_regs->r8 = ((size_t *)service->name)[4];
        currently_executing_thread_regs->r9 = ((size_t *)service->name)[5];
        currently_executing_thread_regs->r10 = ((size_t *)service->name)[6];
        currently_executing_thread_regs->r12 = ((size_t *)service->name)[7];
        currently_executing_thread_regs->r13 = ((size_t *)service->name)[8];
        currently_executing_thread_regs->r14 = ((size_t *)service->name)[9];
      }
      break;
    }
    case NOTIFY_WHEN_SERVICE_APPEARS: {
      // Extract the name from the input registers.
      size_t service_name[SERVICE_NAME_WORDS];
      service_name[0] = currently_executing_thread_regs->rax;
      service_name[1] = currently_executing_thread_regs->rbx;
      service_name[2] = currently_executing_thread_regs->rdx;
      service_name[3] = currently_executing_thread_regs->rsi;
      service_name[4] = currently_executing_thread_regs->r8;
      service_name[5] = currently_executing_thread_regs->r9;
      service_name[6] = currently_executing_thread_regs->r10;
      service_name[7] = currently_executing_thread_regs->r12;
      service_name[8] = currently_executing_thread_regs->r13;
      service_name[9] = currently_executing_thread_regs->r14;

      NotifyProcessWhenServiceAppears((char *)service_name,
                                      running_thread->process,
                                      currently_executing_thread_regs->rbp);
      break;
    }
    case STOP_NOTIFYING_WHEN_SERVICE_APPEARS:
      StopNotifyingProcessWhenServiceAppearsByMessageId(
          running_thread->process, currently_executing_thread_regs->rbp);
      break;
    case NOTIFY_WHEN_SERVICE_DISAPPEARS:
      PrintString("Implement NOTIFY_WHEN_SERVICE_DISAPPEARS\n");
      break;
    case STOP_NOTIFYING_WHEN_SERVICE_DISAPPEARS:
      PrintString("Implement STOP_NOTIFYING_WHEN_SERVICE_DISAPPEARS\n");
      break;
    case SEND_MESSAGE:
      SendMessageFromThreadSyscall(running_thread);
      break;
    case POLL_FOR_MESSAGE:
      LoadNextMessageIntoThread(running_thread);
      break;
    case SLEEP_FOR_MESSAGE:
      if (SleepThreadUntilMessage(running_thread)) {
        // The thread is now asleep. We need to schedule a new thread.
        ScheduleNextThread();
        JumpIntoThread();  // Doesn't return.
      }
      break;
    case REGISTER_MESSAGE_TO_SEND_ON_INTERRUPT:
      RegisterMessageToSendOnInterrupt(
          (int)currently_executing_thread_regs->rax, running_thread->process,
          currently_executing_thread_regs->rbx);
      break;
    case UNREGISTER_MESSAGE_TO_SEND_ON_INTERRUPT:
      UnregisterMessageToSendOnInterrupt(
          (int)currently_executing_thread_regs->rax, running_thread->process,
          currently_executing_thread_regs->rbx);
      break;
    case GET_MULTIBOOT_FRAMEBUFFER_INFORMATION:
      PopulateRegistersWithFramebufferDetails(currently_executing_thread_regs);
      break;
    case SEND_MESSAGE_AFTER_X_MICROSECONDS:
      SendMessageToProcessAtMicroseconds(
          running_thread->process,
          currently_executing_thread_regs->rax +
              GetCurrentTimestampInMicroseconds(),
          (int)currently_executing_thread_regs->rbx);
      break;
    case SEND_MESSAGE_AT_TIMESTAMP:
      SendMessageToProcessAtMicroseconds(
          running_thread->process, currently_executing_thread_regs->rax,
          (int)currently_executing_thread_regs->rbx);
      break;
    case GET_CURRENT_TIMESTAMP:
      currently_executing_thread_regs->rax =
          GetCurrentTimestampInMicroseconds();
      break;
  }
#ifdef PROFILING_ENABLED
  ProfileSyscall(syscall_number, syscall_start_time);
#endif
#ifdef DEBUG
  PrintString("Leaving syscall ");
  PrintString(GetSystemCallName(syscall));
  PrintString(" (") PrintNumber(syscall_number);
  PrintString(" )\n");
  PrintRegisters(currently_executing_thread_regs);
#endif
}

char *GetSystemCallName(int syscall) {
  switch (syscall) {
    default:
      return "Unknown";
    case PRINT_DEBUG_CHARACTER:
      return "PRINT_DEBUG_CHARACTER";
    case PRINT_REGISTERS_AND_STACK:
      return "PRINT_REGISTERS_AND_STACK";
    case CREATE_THREAD:
      return "CREATE_THREAD";
    case GET_THIS_THREAD_ID:
      return "GET_THIS_THREAD_ID";
    case SLEEP_THIS_THREAD:
      return "SLEEP_THIS_THREAD";
    case SLEEP_THREAD:
      return "SLEEP_THREAD";
    case WAKE_THREAD:
      return "WAKE_THREAD";
    case WAKE_AND_SWITCH_TO_THREAD:
      return "WAKE_AND_SWITCH_TO_THREAD";
    case TERMINATE_THIS_THREAD:
      return "TERMINATE_THIS_THREAD";
    case TERMINATE_THREAD:
      return "TERMINATE_THREAD";
    case YIELD:
      return "YIELD";
    case SET_THREAD_SEGMENT:
      return "SET_THREAD_SEGMENT";
    case SET_ADDRESS_TO_CLEAR_ON_THREAD_TERMINATION:
      return "SET_ADDRESS_TO_CLEAR_ON_THREAD_TERMINATION";
    case ALLOCATE_MEMORY_PAGES:
      return "ALLOCATE_MEMORY_PAGES";
    case ALLOCATE_MEMORY_PAGES_BELOW_PHYSICAL_BASE:
      return "ALLOCATE_MEMORY_PAGES_BELOW_PHYSICAL_BASE";
    case RELEASE_MEMORY_PAGES:
      return "RELEASE_MEMORY_PAGES";
    case MAP_PHYSICAL_MEMORY:
      return "MAP_PHYSICAL_MEMORY";
    case GET_PHYSICAL_ADDRESS_OF_VIRTUAL_ADDRESS:
      return "GET_PHYSICAL_ADDRESS_OF_VIRTUAL_ADDRESS";
    case GET_FREE_SYSTEM_MEMORY:
      return "GET_FREE_SYSTEM_MEMORY";
    case GET_MEMORY_USED_BY_PROCESS:
      return "GET_MEMORY_USED_BY_PROCESS";
    case GET_TOTAL_SYSTEM_MEMORY:
      return "GET_TOTAL_SYSTEM_MEMORY";
    case CREATE_SHARED_MEMORY:
      return "CREATE_SHARED_MEMORY";
    case JOIN_SHARED_MEMORY:
      return "JOIN_SHARED_MEMORY";
    case LEAVE_SHARED_MEMORY:
      return "LEAVE_SHARED_MEMORY";
    case MOVE_PAGE_INTO_SHARED_MEMORY:
      return "MOVE_PAGE_INTO_SHARED_MEMORY";
    case IS_SHARED_MEMORY_PAGE_ALLOCATED:
      return "IS_SHARED_MEMORY_PAGE_ALLOCATED";
    case SET_MEMORY_ACCESS_RIGHTS:
      return "SET_MEMORY_ACCESS_RIGHTS";
    case GET_THIS_PROCESS_ID:
      return "GET_THIS_PROCESS_ID";
    case TERMINATE_THIS_PROCESS:
      return "TERMINATE_THIS_PROCESS";
    case TERMINATE_PROCESS:
      return "TERMINATE_PROCESS";
    case GET_PROCESSES:
      return "GET_PROCESSES";
    case GET_NAME_OF_PROCESS:
      return "GET_NAME_OF_PROCESS";
    case NOTIFY_WHEN_PROCESS_DISAPPEARS:
      return "NOTIFY_WHEN_PROCESS_DISAPPEARS";
    case STOP_NOTIFYING_WHEN_PROCESS_DISAPPEARS:
      return "STOP_NOTIFYING_WHEN_PROCESS_DISAPPEARS";
    case REGISTER_SERVICE:
      return "REGISTER_SERVICE";
    case UNREGISTER_SERVICE:
      return "UNREGISTER_SERVICE";
    case GET_SERVICES:
      return "GET_SERVICES";
    case GET_NAME_OF_SERVICE:
      return "GET_NAME_OF_SERVICE";
    case NOTIFY_WHEN_SERVICE_APPEARS:
      return "NOTIFY_WHEN_SERVICE_APPEARS";
    case STOP_NOTIFYING_WHEN_SERVICE_APPEARS:
      return "STOP_NOTIFYING_WHEN_SERVICE_APPEARS";
    case NOTIFY_WHEN_SERVICE_DISAPPEARS:
      return "NOTIFY_WHEN_SERVICE_DISAPPEARS";
    case STOP_NOTIFYING_WHEN_SERVICE_DISAPPEARS:
      return "STOP_NOTIFYING_WHEN_SERVICE_DISAPPEARS";
    case SEND_MESSAGE:
      return "SEND_MESSAGE";
    case POLL_FOR_MESSAGE:
      return "POLL_FOR_MESSAGE";
    case SLEEP_FOR_MESSAGE:
      return "SLEEP_FOR_MESSAGE";
    case REGISTER_MESSAGE_TO_SEND_ON_INTERRUPT:
      return "REGISTER_MESSAGE_TO_SEND_ON_INTERRUPT";
    case UNREGISTER_MESSAGE_TO_SEND_ON_INTERRUPT:
      return "UNREGISTER_MESSAGE_TO_SEND_ON_INTERRUPT";
    case GET_MULTIBOOT_FRAMEBUFFER_INFORMATION:
      return "GET_MULTIBOOT_FRAMEBUFFER_INFORMATION";
    case SEND_MESSAGE_AFTER_X_MICROSECONDS:
      return "SEND_MESSAGE_AFTER_X_MICROSECONDS";
    case SEND_MESSAGE_AT_TIMESTAMP:
      return "SEND_MESSAGE_AT_TIMESTAMP";
    case GET_CURRENT_TIMESTAMP:
      return "GET_CURRENT_TIMESTAMP";
  }
}