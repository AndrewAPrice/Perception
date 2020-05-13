#include "syscall.h"

#include "interrupts.h"
#include "io.h"
#include "messages.h"
#include "process.h"
#include "registers.h"
#include "scheduler.h"
#include "text_terminal.h"
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
// Next id is 22.
#define PRINT_DEBUG_CHARACTER 0
#define CREATE_THREAD 1
#define GET_THIS_THREAD_ID 2
#define SLEEP_THIS_THREAD 3
#define SLEEP_THREAD 9
#define WAKE_THREAD 10
#define WAKE_AND_SWITCH_TO_THREAD 11
#define TERMINATE_THIS_THREAD 4
#define TERMINATE_THREAD 5
#define TERMINATE_THIS_PROCESS 6
#define TERMINATE_PROCESS 7
#define YIELD 8
#define ALLOCATE_MEMORY_PAGES 12
#define RELEASE_MEMORY_PAGES 13
#define GET_FREE_SYSTEM_MEMORY 14
#define GET_MEMORY_USED_BY_PROCESS 15
#define GET_TOTAL_SYSTEM_MEMORY 16
#define GET_PROCESS_BY_NAME 22 // TODO
// Messaging
#define SEND_MESSAGE 17
#define POLL_FOR_MESSAGE 18
#define SLEEP_FOR_MESSAGE 19
// Interrupts
#define REGISTER_MESSAGE_TO_SEND_ON_INTERRUPT 20
#define UNREGISTER_MESSAGE_TO_SEND_ON_INTERRUPT 21
// RPCS
#define REGISTER_RPC 23 // TODO
#define GET_RPC_BY_NAME 24 // TODO
#define CALL_RPC 25 // TODO
#define RETURN_FROM_RPC 26 // TODO

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
		case TERMINATE_THIS_PROCESS:
			DestroyProcess(running_thread->process);
			JumpIntoThread(); // Doesn't return.
			break;
		case TERMINATE_PROCESS:
			PrintString("Implement TERMINATE_PROCESS\n");
			break;
		case YIELD:
			ScheduleNextThread();
			JumpIntoThread(); // Doesn't return.
			break;
		case ALLOCATE_MEMORY_PAGES:
			currently_executing_thread_regs->rax =
				AllocateVirtualMemoryInAddressSpace(running_thread->process->pml4,
					currently_executing_thread_regs->rax);
			break;
		case RELEASE_MEMORY_PAGES: {
			ReleaseVirtualMemoryInAddressSpace(running_thread->process->pml4,
					currently_executing_thread_regs->rax,
					currently_executing_thread_regs->rbx);
			break;
		}
		case GET_FREE_SYSTEM_MEMORY:
			currently_executing_thread_regs->rax = free_pages * PAGE_SIZE;
			break;
		case GET_MEMORY_USED_BY_PROCESS:
			currently_executing_thread_regs->rax = running_thread->process->allocated_pages * PAGE_SIZE;
			break;
		case GET_TOTAL_SYSTEM_MEMORY:
			currently_executing_thread_regs->rax = total_system_memory;
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
	}
#ifdef DEBUG
	PrintString("Leaving syscall: ");
	PrintNumber(syscall_number);
	PrintChar('\n');
	PrintRegisters(currently_executing_thread_regs);
#endif
}
