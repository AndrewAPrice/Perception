#include "syscall.h"

#include "interrupts.h"
#include "io.h"
#include "framebuffer.h"
#include "messages.h"
#include "process.h"
#include "registers.h"
#include "scheduler.h"
#include "service.h"
#include "shared_memory.h"
#include "text_terminal.h"
#include "timer.h"
#include "physical_allocator.h"
#include "thread.h"
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
	wrmsr(STAR, KERNEL_SEGMENT_BASE | USER_SEGMENT_BASE);
	wrmsr(LSTAR, (size_t)syscall_entry);
	// Disable interrupts duing syscalls.
	wrmsr(IA32_FMASK, INTERRUPT_MASK);
//	SetInterruptHandler(0x80, (size_t)syscall_isr, 0x08, 0x8E);
}

// Syscalls.
// Next id is 45.
// Free: 26
#define PRINT_DEBUG_CHARACTER 0
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
#define RELEASE_MEMORY_PAGES 13
#define MAP_PHYSICAL_MEMORY 41
#define GET_FREE_SYSTEM_MEMORY 14
#define GET_MEMORY_USED_BY_PROCESS 15
#define GET_TOTAL_SYSTEM_MEMORY 16
#define CREATE_SHARED_MEMORY 42
#define JOIN_SHARED_MEMORY 43
#define LEAVE_SHARED_MEMORY 44
// Processes
#define GET_THIS_PROCESS_ID 39
#define TERMINATE_THIS_PROCESS 6
#define TERMINATE_PROCESS 7
#define GET_PROCESS_BY_NAME 22
#define GET_NAME_OF_PROCESS 29
#define NOTIFY_WHEN_PROCESS_DISAPPEARS 30
#define STOP_NOTIFYING_WHEN_PROCESS_DISAPPEARS 31
// Services
#define REGISTER_SERVICE 32
#define UNREGISTER_SERVICE 33
#define GET_SERVICE_BY_NAME 34
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
	PrintString("Entering syscall: ");
	PrintNumber(syscall_number);
	PrintChar('\n');
	PrintRegisters(currently_executing_thread_regs);
#endif
	switch (syscall_number) {
		case PRINT_DEBUG_CHARACTER:
			PrintChar((unsigned char)currently_executing_thread_regs->rax);
			break;
		case CREATE_THREAD: {
			struct Thread* new_thread = CreateThread(running_thread->process,
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
			JumpIntoThread(); // Doesn't return.
			break;
		case TERMINATE_THREAD: {
			struct Thread* thread = GetThreadFromTid(
				running_thread->process, currently_executing_thread_regs->rax);
			if (thread == running_thread) {
				DestroyThread(running_thread, false);
				JumpIntoThread(); // Doesn't return.
			} else if (thread != 0) {
				DestroyThread(thread, false);
			}
			break;
		}
		case YIELD:
			ScheduleNextThread();
			JumpIntoThread(); // Doesn't return.
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
				AllocateVirtualMemoryInAddressSpace(running_thread->process->pml4,
					currently_executing_thread_regs->rax);
			break;
		case RELEASE_MEMORY_PAGES:
			ReleaseVirtualMemoryInAddressSpace(running_thread->process->pml4,
					currently_executing_thread_regs->rax,
					currently_executing_thread_regs->rbx);
			break;
		case MAP_PHYSICAL_MEMORY:
			// Only drivers can map physical memory.
			if (running_thread->process->is_driver) {
				currently_executing_thread_regs->rax =
					MapPhysicalMemoryInAddressSpace(running_thread->process->pml4,
						currently_executing_thread_regs->rax,
						currently_executing_thread_regs->rbx);
			} else {
				currently_executing_thread_regs->rax =
					OUT_OF_MEMORY;
			}
			break;
		case GET_FREE_SYSTEM_MEMORY:
			currently_executing_thread_regs->rax = free_pages * PAGE_SIZE;
			break;
		case GET_MEMORY_USED_BY_PROCESS:
			currently_executing_thread_regs->rax = running_thread->process->allocated_pages * PAGE_SIZE;
			break;
		case GET_TOTAL_SYSTEM_MEMORY:
			currently_executing_thread_regs->rax = total_system_memory;
			break;
		case CREATE_SHARED_MEMORY: {
			struct SharedMemoryInProcess* shared_memory =
				CreateAndMapSharedMemoryBlockIntoProcess(
					running_thread->process,
					currently_executing_thread_regs->rax);
			if (shared_memory == NULL) {
				// Could not create the shared memory block.
				currently_executing_thread_regs->rax = 0;
				currently_executing_thread_regs->rbx = 0;
			} else {
				// Created the shared memory block.
				currently_executing_thread_regs->rax =
					shared_memory->shared_memory->id;
				currently_executing_thread_regs->rbx =
					shared_memory->virtual_address;
			}
			break;
		}
		case JOIN_SHARED_MEMORY: {
			struct SharedMemoryInProcess* shared_memory =
				JoinSharedMemory(
					running_thread->process,
					currently_executing_thread_regs->rax);

			if (shared_memory == NULL) {
				// Could not join the shared memory block.
				currently_executing_thread_regs->rax = 0;
				currently_executing_thread_regs->rbx = 0;
			} else {
				// Joined the shared memory block.
				currently_executing_thread_regs->rax =
					shared_memory->shared_memory->size_in_pages;
				currently_executing_thread_regs->rbx =
					shared_memory->virtual_address;
			}
			break;
		}
		case LEAVE_SHARED_MEMORY:
			LeaveSharedMemory(
				running_thread->process,
				currently_executing_thread_regs->rax);
			break;
		case GET_THIS_PROCESS_ID:
			currently_executing_thread_regs->rax = running_thread->process->pid;
			break;
		case TERMINATE_THIS_PROCESS:
			DestroyProcess(running_thread->process);
			JumpIntoThread(); // Doesn't return.
			break;
		case TERMINATE_PROCESS: {
			struct Process* process =
				GetProcessFromPid(currently_executing_thread_regs->rax);
			if (process == NULL) {
				break;
			}
			bool currently_running_process = process == running_thread->process;
			DestroyProcess(process);
			if (currently_running_process) {
				JumpIntoThread(); // Doesn't return.
			}
			break;
		}
		case GET_PROCESS_BY_NAME: {
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
			struct Process* process =
				GetProcessOrNextFromPid(currently_executing_thread_regs->rbp);
			while (process != NULL) {
				process = FindNextProcessWithName((char *)process_name, process);
				if (process != NULL) {
					if (processes_found < 12)
						pids[processes_found] = process->pid;

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
			struct Process* process =
				GetProcessFromPid(currently_executing_thread_regs->rax);
			if (process == NULL) {
				currently_executing_thread_regs->rdi = 0;
			} else {
				currently_executing_thread_regs->rdi = 1;
				currently_executing_thread_regs->rax =
					((size_t *)process->name)[0];
				currently_executing_thread_regs->rbx =
					((size_t *)process->name)[1];
				currently_executing_thread_regs->rdx =
					((size_t *)process->name)[2];
				currently_executing_thread_regs->rsi =
					((size_t *)process->name)[3];
				currently_executing_thread_regs->r8 =
					((size_t *)process->name)[4];
				currently_executing_thread_regs->r9 =
					((size_t *)process->name)[5];
				currently_executing_thread_regs->r10 =
					((size_t *)process->name)[6];
				currently_executing_thread_regs->r12 =
					((size_t *)process->name)[7];
				currently_executing_thread_regs->r13 =
					((size_t *)process->name)[8];
				currently_executing_thread_regs->r14 =
					((size_t *)process->name)[9];
				currently_executing_thread_regs->r15 =
					((size_t *)process->name)[10];
			}
			break;
		}
		case NOTIFY_WHEN_PROCESS_DISAPPEARS: {
			size_t target_pid = currently_executing_thread_regs->rax;
			size_t event_id = currently_executing_thread_regs->rbx;

			struct Process* target =
				GetProcessFromPid(currently_executing_thread_regs->rax);
			if (target == NULL) {
				// The target process we want to be notified of when it dies
				// doesn't exist. It's possible that is just died, so whatever
				// the case, the safest thing to do here is to imemdiately send
				// an event.
				SendKernelMessageToProcess(
					running_thread->process,
					event_id,
					target_pid, 0, 0, 0, 0);
			} else {
				NotifyProcessOnDeath(target, running_thread->process,
					event_id);
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
			PrintString((char*)service_name);
			PrintString(" / ");
			PrintNumber(currently_executing_thread_regs->rbp);
			PrintString(" in process ");
			PrintNumber(running_thread->process->pid);
			PrintString("\n");
#endif

			RegisterService((char*)service_name, running_thread->process,
				currently_executing_thread_regs->rbp);
			break;
		}
		case UNREGISTER_SERVICE:
			UnregisterServiceByMessageId(running_thread->process,
				currently_executing_thread_regs->rax);
			break;
		case GET_SERVICE_BY_NAME: {
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
			for (struct Service* service =
					FindNextServiceByPidAndMidWithName((char *)service_name,
						min_pid, min_sid);
				service != NULL;
				service = FindNextServiceWithName(
					(char *)service_name, service)) {
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

			NotifyProcessWhenServiceAppears((char*)service_name,
				running_thread->process,
				currently_executing_thread_regs->rbp);
			break;
		}
		case STOP_NOTIFYING_WHEN_SERVICE_APPEARS:
			StopNotifyingProcessWhenServiceAppearsByMessageId(
				running_thread->process,
				currently_executing_thread_regs->rbp);
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
				JumpIntoThread(); // Doesn't return.
			}
			break;
		case REGISTER_MESSAGE_TO_SEND_ON_INTERRUPT:
			RegisterMessageToSendOnInterrupt(
				(int)currently_executing_thread_regs->rax,
				running_thread->process,
				currently_executing_thread_regs->rbx);
			break;
		case UNREGISTER_MESSAGE_TO_SEND_ON_INTERRUPT:
			UnregisterMessageToSendOnInterrupt(
				(int)currently_executing_thread_regs->rax,
				running_thread->process,
				currently_executing_thread_regs->rbx);
			break;
		case GET_MULTIBOOT_FRAMEBUFFER_INFORMATION:
			PopulateRegistersWithFramebufferDetails(
				currently_executing_thread_regs);
			break;
		case SEND_MESSAGE_AFTER_X_MICROSECONDS:
			SendMessageToProcessAtMicroseconds(running_thread->process,
				currently_executing_thread_regs->rax +
					GetCurrentTimestampInMicroseconds(),
				(int)currently_executing_thread_regs->rbx);
			break;
		case SEND_MESSAGE_AT_TIMESTAMP:
			SendMessageToProcessAtMicroseconds(running_thread->process,
				currently_executing_thread_regs->rax,
				(int)currently_executing_thread_regs->rbx);
			break;
		case GET_CURRENT_TIMESTAMP:
			currently_executing_thread_regs->rax =
				GetCurrentTimestampInMicroseconds();
			break;
	}
#ifdef DEBUG
	PrintString("Leaving syscall: ");
	PrintNumber(syscall_number);
	PrintChar('\n');
	PrintRegisters(currently_executing_thread_regs);
#endif
}
