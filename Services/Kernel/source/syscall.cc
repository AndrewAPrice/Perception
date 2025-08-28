#include "syscall.h"

#include "framebuffer.h"
#include "interrupts.asm.h"
#include "interrupts.h"
#include "io.h"
#include "messages.h"
#include "multiboot_modules.h"
#include "physical_allocator.h"
#include "process.h"
#include "profiling.h"
#include "registers.h"
#include "scheduler.h"
#include "service.h"
#include "shared_memory.h"
#include "stack_trace.h"
#include "syscall.asm.h"
#include "syscalls.h"
#include "text_terminal.h"
#include "thread.h"
#include "timer.h"
#include "virtual_address_space.h"
#include "virtual_allocator.h"

namespace {

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

}  // namespace

void InitializeSystemCalls() {
#ifndef __TEST__
  WriteModelSpecificRegister(STAR, KERNEL_SEGMENT_BASE | USER_SEGMENT_BASE);
  WriteModelSpecificRegister(LSTAR, (size_t)syscall_entry);
  // Disable interrupts duing syscalls.
  WriteModelSpecificRegister(IA32_FMASK, INTERRUPT_MASK);
//  SetInterruptHandler(0x80, (size_t)syscall_isr, 0x08, 0x8E);
#endif
}

int last_printing_process = -1;
bool last_char_was_newline = false;

extern "C" void SyscallHandler(int syscall_number) {
  switch (static_cast<Syscall>(syscall_number)) {
    default:
      print << "Syscall " << GetSystemCallName(syscall_number) << " ("
            << NumberFormat::Decimal << syscall_number;
      if (running_thread) {
        print << ") from " << running_thread->process->name << " ("
              << running_thread->process->pid;
      }
      print << ") is unimplemented.\n";
      break;
    case Syscall::PrintDebugCharacter: {
      if (last_printing_process != running_thread->process->pid) {
        if (!last_char_was_newline) print << '\n';
        print << running_thread->process->name << ": ";
        last_printing_process = running_thread->process->pid;
      }
      char c = (char)currently_executing_thread_regs->rax;
      print << c;
      last_char_was_newline = (c == '\n');
      break;
    }
    case Syscall::PrintRegistersAndStack: {
      print << "Dump requested by PID " << NumberFormat::Decimal
            << running_thread->process->pid << " ("
            << running_thread->process->name << ") in TID "
            << running_thread->id << '\n';
      PrintRegistersAndStackTrace();
      break;
    }
    case Syscall::CreateThread: {
      Thread *new_thread = CreateThread(running_thread->process,
                                        currently_executing_thread_regs->rax,
                                        currently_executing_thread_regs->rbx);
      if (new_thread == 0) {
        currently_executing_thread_regs->rax = 0;
      } else {
        currently_executing_thread_regs->rax = new_thread->id;
      }
      ScheduleThread(new_thread);
      break;
    }
    case Syscall::GetThisThreadId:
      currently_executing_thread_regs->rax = running_thread->id;
      break;
      /*
        case Syscall::SleepThisThread:
         print << "Implement Syscall::SleepThread\n";
         break;
       case Syscall::SleepThread:
         print << "Implement SLEEP\n";
         break;
       case Syscall::WakeThread:
         print << "Implement Syscall::WakeThread\n";
         // TODO: if thread is waiting for event, set bad event id
         break;
       case Syscall::WaitAndSwitchToThread:
         print << "Implement Syscall::WaitAndSwitchToThread\n";
         // TODO: if thread is waiting for event, set bad event id
         break;
         */
    case Syscall::TerminateThisThread:
      DestroyThread(running_thread, false);
      JumpIntoThread();  // Doesn't return.
      break;
    case Syscall::TerminateThread: {
      Thread *thread = GetThreadFromTid(running_thread->process,
                                        currently_executing_thread_regs->rax);
      if (thread == running_thread) {
        DestroyThread(running_thread, false);
        JumpIntoThread();  // Doesn't return.
      } else if (thread != 0) {
        DestroyThread(thread, false);
      }
      break;
    }
    case Syscall::Yield:
      ScheduleNextThread();
      JumpIntoThread();  // Doesn't return.
      break;
    case Syscall::SetThreadSegment:
      SetThreadSegment(running_thread, currently_executing_thread_regs->rax);
      break;
    case Syscall::SetAddressToClearOnThreadTermination:
      // Align the address to 8 bytes to avoid crossing page boundaries.
      running_thread->address_to_clear_on_termination =
          currently_executing_thread_regs->rax & (~7L);
      break;
    case Syscall::AllocateMemoryPages:
      currently_executing_thread_regs->rax =
          running_thread->process->virtual_address_space.AllocatePages(
              currently_executing_thread_regs->rax);
      break;
    case Syscall::AllocateMemoryPagesBelowPhysicalBase:
      if (running_thread->process->is_driver) {
        size_t first_physical_address = 0;
        currently_executing_thread_regs->rax =
            running_thread->process->virtual_address_space
                .AllocatePagesBelowMaxBaseAddress(
                    currently_executing_thread_regs->rax,
                    currently_executing_thread_regs->rbx);
        currently_executing_thread_regs->rbx =
            running_thread->process->virtual_address_space.GetPhysicalAddress(
                currently_executing_thread_regs->rax,
                /*ignore_unowned_pages=*/false);
      } else {
        currently_executing_thread_regs->rax = OUT_OF_MEMORY;
        currently_executing_thread_regs->rbx = 0;
      }
      break;
    case Syscall::ReleaseMemoryPages:
      running_thread->process->virtual_address_space.FreePages(
          currently_executing_thread_regs->rax,
          currently_executing_thread_regs->rbx);
      break;
    case Syscall::MapPhysicalMemory:
      // Only drivers can map physical memory.
      if (running_thread->process->is_driver) {
        currently_executing_thread_regs->rax =
            running_thread->process->virtual_address_space.MapPhysicalPages(
                currently_executing_thread_regs->rax,
                currently_executing_thread_regs->rbx);
      } else {
        currently_executing_thread_regs->rax = OUT_OF_MEMORY;
      }
      break;
    case Syscall::GetPhysicalAddressOfVirtualAddress:
      if (running_thread->process->is_driver) {
        currently_executing_thread_regs->rax =
            running_thread->process->virtual_address_space.GetPhysicalAddress(
                currently_executing_thread_regs->rax,
                /*ignore_unowned_pages=*/false);
      } else {
        currently_executing_thread_regs->rax = 0;
      }
      break;
    case Syscall::GetFreeSystemMemory:
      currently_executing_thread_regs->rax = free_pages * PAGE_SIZE;
      break;
    case Syscall::GetMemoryUsedByProcess:
      currently_executing_thread_regs->rax =
          running_thread->process->allocated_pages * PAGE_SIZE;
      break;
    case Syscall::GetTotalSystemMemory:
      currently_executing_thread_regs->rax = total_system_memory;
      break;
    case Syscall::CreateSharedMemory: {
      SharedMemoryInProcess *shared_memory =
          CreateAndMapSharedMemoryBlockIntoProcess(
              running_thread->process, currently_executing_thread_regs->rax,
              currently_executing_thread_regs->rbx,
              currently_executing_thread_regs->rdx);
      if (shared_memory == nullptr) {
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
    case Syscall::JoinSharedMemory: {
      SharedMemoryInProcess *shared_memory = JoinSharedMemory(
          running_thread->process, currently_executing_thread_regs->rax);

      if (shared_memory == nullptr) {
        // Could not join the shared memory block.
        currently_executing_thread_regs->rax = 0;
        currently_executing_thread_regs->rbx = 0;
        currently_executing_thread_regs->rdx = 0;
      } else {
        // Joined the shared memory block.
        currently_executing_thread_regs->rax = shared_memory->mapped_pages;
        currently_executing_thread_regs->rbx = shared_memory->virtual_address;
        currently_executing_thread_regs->rdx =
            shared_memory->shared_memory->flags;
      }
      break;
    }
    case Syscall::JoinChildProcessInSharedMemory: {
      Process *child_process =
          GetProcessFromPid(currently_executing_thread_regs->rax);
      currently_executing_thread_regs->rax =
          (bool)JoinChildProcessInSharedMemory(
              running_thread->process, child_process,
              currently_executing_thread_regs->rbx,
              currently_executing_thread_regs->rdx);
      break;
    }
    case Syscall::LeaveSharedMemory:
      LeaveSharedMemory(running_thread->process,
                        currently_executing_thread_regs->rax);
      break;
    case Syscall::GetSharedMemoryDetails:
      GetSharedMemoryDetailsPertainingToProcess(
          running_thread->process, currently_executing_thread_regs->rax,
          currently_executing_thread_regs->rax,
          currently_executing_thread_regs->rbx);

      break;
    case Syscall::MovePageIntoSharedMemory:
      MovePageIntoSharedMemory(running_thread->process,
                               currently_executing_thread_regs->rax,
                               currently_executing_thread_regs->rbx,
                               currently_executing_thread_regs->rdx);
      break;
    case Syscall::GrantPermissionToAllocateIntoSharedMemory:
      GrantPermissionToAllocateIntoSharedMemory(
          running_thread->process, currently_executing_thread_regs->rax,
          currently_executing_thread_regs->rbx);
      break;
    case Syscall::IsSharedMemoryPageAllocated:
      currently_executing_thread_regs->rax =
          IsAddressAllocatedInSharedMemory(currently_executing_thread_regs->rax,
                                           currently_executing_thread_regs->rbx)
              ? 1
              : 0;
      break;
    case Syscall::GetSharedMemoryPagePhysicalAddress:
      if (running_thread->process->is_driver) {
        currently_executing_thread_regs->rax =
            GetPhysicalAddressOfPageInSharedMemory(
                currently_executing_thread_regs->rax,
                currently_executing_thread_regs->rbx);
      } else {
        currently_executing_thread_regs->rax = OUT_OF_MEMORY;
      }
      break;
    case Syscall::GrowSharedMemory: {
      SharedMemoryInProcess *shared_memory = GrowSharedMemory(
          running_thread->process, currently_executing_thread_regs->rax,
          currently_executing_thread_regs->rbx);

      if (shared_memory == nullptr) {
        // Could not grow the shared memory block.
        currently_executing_thread_regs->rax = 0;
        currently_executing_thread_regs->rbx = 0;
      } else {
        // Grew the shared memory block.
        currently_executing_thread_regs->rax =
            shared_memory->shared_memory->size_in_pages;
        currently_executing_thread_regs->rbx = shared_memory->virtual_address;
      }
      break;
    }
    case Syscall::SetMemoryAccessRights: {
      size_t address = currently_executing_thread_regs->rax;
      size_t num_pages = currently_executing_thread_regs->rbx;
      size_t max_address = address + num_pages * PAGE_SIZE;
      size_t rights = currently_executing_thread_regs->rdx;

      for (; address < max_address; address += PAGE_SIZE) {
        running_thread->process->virtual_address_space.SetMemoryAccessRights(
            address, rights);
      }
      break;
    }
    case Syscall::GetThisProcessId:
      currently_executing_thread_regs->rax = running_thread->process->pid;
      break;
    case Syscall::TerminateThisProcess:
      DestroyProcess(running_thread->process);
      JumpIntoThread();  // Doesn't return.
      break;
    case Syscall::TerminateProcess: {
      Process *process =
          GetProcessFromPid(currently_executing_thread_regs->rax);
      if (process == nullptr) {
        break;
      }
      bool currently_running_process = process == running_thread->process;
      DestroyProcess(process);
      if (currently_running_process) {
        JumpIntoThread();  // Doesn't return.
      }
      break;
    }
    case Syscall::GetProcesses: {
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
      // until processes run out. Keep track of the pids of the
      // first 12 found.
      size_t pids[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
      size_t processes_found = 0;
      Process *process =
          GetProcessOrNextFromPid(currently_executing_thread_regs->rbp);
      while (process != nullptr) {
        process = FindNextProcessWithName((char *)process_name, process);
        if (process != nullptr) {
          if (processes_found < 12) pids[processes_found] = process->pid;

          processes_found++;
          process = GetNextProcess(process);
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
    case Syscall::GetNameOfProcess: {
      Process *process =
          GetProcessFromPid(currently_executing_thread_regs->rax);
      if (process == nullptr) {
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
    case Syscall::NotifyWhenProcessDisappears: {
      size_t target_pid = currently_executing_thread_regs->rax;
      size_t event_id = currently_executing_thread_regs->rbx;

      Process *target = GetProcessFromPid(currently_executing_thread_regs->rax);
      if (target == nullptr) {
        // The target process to be notified of when it dies doesn't
        // exist. It's possible that it just died, so whatever the
        // case, the safest thing to do here is to immediately send an
        // event.
        SendKernelMessageToProcess(running_thread->process, event_id,
                                   target_pid, 0, 0, 0, 0);
      } else {
        NotifyProcessOnDeath(target, running_thread->process, event_id);
      }
      break;
    }
    case Syscall::StopNotifyingWhenProcessDisappears:
      StopNotifyingProcessOnDeath(running_thread->process,
                                  currently_executing_thread_regs->rax);
      break;
    case Syscall::CreateProcess: {
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

      Process *child_process =
          CreateChildProcess(running_thread->process, (char *)process_name,
                             currently_executing_thread_regs->rdi);
      currently_executing_thread_regs->rax =
          ((size_t)child_process == ERROR) ? 0 : child_process->pid;
      break;
    }
    case Syscall::SetChildProcessMemoryPage: {
      Process *child_process =
          GetProcessFromPid(currently_executing_thread_regs->rax);
      SetChildProcessMemoryPage(running_thread->process, child_process,
                                currently_executing_thread_regs->rbx,
                                currently_executing_thread_regs->rdx);
      break;
    }
    case Syscall::StartExecutionProcess: {
      Process *child_process =
          GetProcessFromPid(currently_executing_thread_regs->rax);
      StartExecutingChildProcess(running_thread->process, child_process,
                                 currently_executing_thread_regs->rbx,
                                 currently_executing_thread_regs->rdx);
      break;
    }
    case Syscall::DestroyChildProcess: {
      Process *child_process =
          GetProcessFromPid(currently_executing_thread_regs->rax);
      DestroyChildProcess(running_thread->process, child_process);
      break;
    }
    case Syscall::GetMultibootModule: {
      size_t module_name[MODULE_NAME_WORDS];

      LoadNextMultibootModuleIntoProcess(
          running_thread->process,
          /*address_and_flags=*/currently_executing_thread_regs->rdi,
          /*size=*/currently_executing_thread_regs->rbp, (char *)module_name);
      currently_executing_thread_regs->rax = ((size_t *)module_name)[0];
      currently_executing_thread_regs->rbx = ((size_t *)module_name)[1];
      currently_executing_thread_regs->rdx = ((size_t *)module_name)[2];
      currently_executing_thread_regs->rsi = ((size_t *)module_name)[3];
      currently_executing_thread_regs->r8 = ((size_t *)module_name)[4];
      currently_executing_thread_regs->r9 = ((size_t *)module_name)[5];
      currently_executing_thread_regs->r10 = ((size_t *)module_name)[6];
      currently_executing_thread_regs->r12 = ((size_t *)module_name)[7];
      currently_executing_thread_regs->r13 = ((size_t *)module_name)[8];
      currently_executing_thread_regs->r14 = ((size_t *)module_name)[9];
      currently_executing_thread_regs->r15 = ((size_t *)module_name)[10];
      break;
    }
    case Syscall::RegisterService: {
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

      RegisterService((char *)service_name, running_thread->process,
                      currently_executing_thread_regs->rbp);
      break;
    }
    case Syscall::UnregisterService:
      UnregisterServiceByMessageId(running_thread->process,
                                   currently_executing_thread_regs->rax);
      break;
    case Syscall::GetServices: {
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
      // and services starting from the provided SID until processes
      // run out. The pids and sids of the first 6 found will be tracked.
      size_t pids[6] = {0, 0, 0, 0, 0, 0};
      size_t sids[6] = {0, 0, 0, 0, 0, 0};
      size_t services_found = 0;
      for (Service *service = FindNextServiceByPidAndMidWithName(
               (char *)service_name, min_pid, min_sid);
           service != nullptr;
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
    case Syscall::GetNameOfService: {
      size_t pid = currently_executing_thread_regs->rax;
      size_t sid = currently_executing_thread_regs->rbx;
      Service *service = FindServiceByProcessAndMid(pid, sid);
      if (service == nullptr) {
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
    case Syscall::NotifyWhenServiceAppears: {
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
    case Syscall::StopNotifyingWhenServiceAppears:
      StopNotifyingProcessWhenServiceAppearsByMessageId(
          running_thread->process, currently_executing_thread_regs->rbp);
      break;
    case Syscall::NotifyWhenServiceDisappears:
      NotifyProcessWhenServiceDisappears(
          running_thread->process,
          /*service_process_id=*/currently_executing_thread_regs->rax,
          /*service_message_id=*/currently_executing_thread_regs->rbx,
          /*message_id=*/currently_executing_thread_regs->rdx);
      break;
    case Syscall::StopNotifyingWhenServiceDisappears:
      StopNotifyingProcessWhenServiceDisappears(
          running_thread->process,
          /*message_id=*/currently_executing_thread_regs->rax);
      break;
    case Syscall::SendMessage:
      SendMessageFromThreadSyscall(running_thread);
      break;
    case Syscall::PollForMessage:
      LoadNextMessageIntoThread(running_thread);
      break;
    case Syscall::SleepForMessage:
      if (SleepThreadUntilMessage(running_thread)) {
        // The thread is now asleep. A new thread needs to be scheduled.
        ScheduleNextThread();
        JumpIntoThread();  // Doesn't return.
      }
      break;
    case Syscall::RegisterMessageToSendOnInterrupt:
      RegisterMessageToSendOnInterrupt(
          (int)currently_executing_thread_regs->rax, running_thread->process,
          currently_executing_thread_regs->rbx,
          currently_executing_thread_regs->rdx,
          currently_executing_thread_regs->rsi);
      break;
    case Syscall::UnregisterMessageToSendOnInterrupt:
      UnregisterMessageToSendOnInterrupt(
          (int)currently_executing_thread_regs->rax, running_thread->process,
          currently_executing_thread_regs->rbx);
      break;
    case Syscall::GetMultibootFramebufferInformation:
      PopulateRegistersWithFramebufferDetails(currently_executing_thread_regs);
      break;
    case Syscall::SendMessageAfterXMicroseconds:
      SendMessageToProcessAtMicroseconds(
          running_thread->process,
          currently_executing_thread_regs->rax +
              GetCurrentTimestampInMicroseconds(),
          (int)currently_executing_thread_regs->rbx);
      break;
    case Syscall::SendMessageAtTimestamp:
      SendMessageToProcessAtMicroseconds(
          running_thread->process, currently_executing_thread_regs->rax,
          (int)currently_executing_thread_regs->rbx);
      break;
    case Syscall::GetCurrentTimestamp:
      currently_executing_thread_regs->rax =
          GetCurrentTimestampInMicroseconds();
      break;
    case Syscall::EnableProfiling:
      EnableProfiling(running_thread->process);
      break;
    case Syscall::DisableAndOutputProfiling:
      DisableAndOutputProfiling(running_thread->process);
      break;
  }
}
