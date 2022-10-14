// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "interrupts.h"

#include "exceptions.h"
#include "idt.h"
#include "io.h"
#include "liballoc.h"
#include "messages.h"
#include "physical_allocator.h"
#include "process.h"
#include "scheduler.h"
#include "text_terminal.h"
#include "timer.h"
#include "tss.h"
#include "virtual_allocator.h"

// #define DEBUG

extern void irq0();
extern void irq1();
extern void irq2();
extern void irq3();
extern void irq4();
extern void irq5();
extern void irq6();
extern void irq7();
extern void irq8();
extern void irq9();
extern void irq10();
extern void irq11();
extern void irq12();
extern void irq13();
extern void irq14();
extern void irq15();

// The top of the interrupt's stack.
size_t interrupt_stack_top;

// A message to fire on an interrupt.
struct MessageToFireOnInterrupt {
  // The process to send the message to.
  struct Process* process;

  // The message ID to fire.
  size_t message_id;

  // The interrupt number.
  uint8 interrupt_number;

  // Next message to fire for this interrupt.
  struct MessageToFireOnInterrupt* next_message_for_interrupt;

  // Next message for this process.
  struct MessageToFireOnInterrupt* next_message_for_process;
};

// A list of messages pointers to our IRQ handlers.
struct MessageToFireOnInterrupt* messages_to_fire_on_interrupt[16] = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

// Remaps hardware interrupts 0->15 to 32->47 on the Interrupt Descriptor Table
// to not overlap with CPU exceptions.
void RemapIrqsToNotOverlapWithCpuExceptions() {
  outportb(0x20, 0x11);
  outportb(0xA0, 0x11);
  outportb(0x21, 0x20);
  outportb(0xA1, 0x28);
  outportb(0x21, 0x04);
  outportb(0xA1, 0x02);
  outportb(0x21, 0x01);
  outportb(0xA1, 0x01);
  outportb(0x21, 0x0);
  outportb(0xA1, 0x0);
}

// Registers the 16 hardware interrupt handlers.
void RegisterInterruptHandlers() {
  RemapIrqsToNotOverlapWithCpuExceptions();

  SetIdtEntry(32, (size_t)irq0, 0x08, 0x8E);
  SetIdtEntry(33, (size_t)irq1, 0x08, 0x8E);
  SetIdtEntry(34, (size_t)irq2, 0x08, 0x8E);
  SetIdtEntry(35, (size_t)irq3, 0x08, 0x8E);
  SetIdtEntry(36, (size_t)irq4, 0x08, 0x8E);
  SetIdtEntry(37, (size_t)irq5, 0x08, 0x8E);
  SetIdtEntry(38, (size_t)irq6, 0x08, 0x8E);
  SetIdtEntry(39, (size_t)irq7, 0x08, 0x8E);
  SetIdtEntry(40, (size_t)irq8, 0x08, 0x8E);
  SetIdtEntry(41, (size_t)irq9, 0x08, 0x8E);
  SetIdtEntry(42, (size_t)irq10, 0x08, 0x8E);
  SetIdtEntry(43, (size_t)irq11, 0x08, 0x8E);
  SetIdtEntry(44, (size_t)irq12, 0x08, 0x8E);
  SetIdtEntry(45, (size_t)irq13, 0x08, 0x8E);
  SetIdtEntry(46, (size_t)irq14, 0x08, 0x8E);
  SetIdtEntry(47, (size_t)irq15, 0x08, 0x8E);
}

// Allocates a stack to use for interrupts.
void AllocateInterruptStack() {
  size_t physical_addr = GetPhysicalPage();
  size_t virtual_addr = FindFreePageRange(kernel_pml4, 1);
  if (physical_addr == OUT_OF_PHYSICAL_PAGES || virtual_addr == OUT_OF_MEMORY) {
    PrintString("Out of memory to allocate interrupt stack.");
    __asm__ __volatile__("cli");
    __asm__ __volatile__("hlt");
  }

  MapPhysicalPageToVirtualPage(kernel_pml4, virtual_addr, physical_addr, true,
                               true, false);

#ifdef DEBUG
  PrintString("Interrupt stack is at ");
  PrintHex(virtual_addr);
  PrintChar('\n');
#endif

  interrupt_stack_top = virtual_addr + PAGE_SIZE;

  SetInterruptStack(virtual_addr);
}

// Initializes interrupts.
void InitializeInterrupts() {
  InitializeIdt();
  AllocateInterruptStack();

  // There are two sets of interrupts - CPU exceptions and hardware signals.
  // We'll register handler for both.
  RegisterExceptionInterrupts();
  RegisterInterruptHandlers();
}

// Registers a message to send to a process upon receiving an interrupt.
void RegisterMessageToSendOnInterrupt(size_t interrupt_number,
                                      struct Process* process,
                                      size_t message_id) {
  if (!process->is_driver) {
    // Only drivers can listen to interrupts.
    return;
  }

  interrupt_number &= 0xF;

  struct MessageToFireOnInterrupt* message =
      malloc(sizeof(struct MessageToFireOnInterrupt));
  message->process = process;
  message->message_id = message_id;
  message->interrupt_number = (uint8)interrupt_number;

  // Add to the linked list of messages for this interrupt.
  message->next_message_for_interrupt =
      messages_to_fire_on_interrupt[interrupt_number];
  messages_to_fire_on_interrupt[interrupt_number] = message;

  // Add to the linked list of messages for this process.
  message->next_message_for_process = process->message_to_fire_on_interrupt;
  process->message_to_fire_on_interrupt = message;
}

// Unregisters a message to send to a process upon receiving an interrupt.
void UnregisterMessageToSendOnInterrupt(size_t interrupt_number,
                                        struct Process* process,
                                        size_t message_id) {
  if (!process->is_driver) {
    // Only drivers can listen to interrupts.
    return;
  }

  interrupt_number &= 0xF;

  // Remove all matching messages from the interrupt's list.
  struct MessageToFireOnInterrupt* previous = NULL;
  struct MessageToFireOnInterrupt* current =
      messages_to_fire_on_interrupt[interrupt_number];
  while (current != NULL) {
    struct MessageToFireOnInterrupt* next = current->next_message_for_interrupt;
    if (current->process == process && current->message_id == message_id) {
      // We found the message!
      if (previous == NULL) {
        messages_to_fire_on_interrupt[interrupt_number] = next;
      } else {
        previous->next_message_for_interrupt = next;
      }
    } else {
      previous = current;
    }
    current = next;
  }

  // Remove (and release) all matching messages from the process's list.
  previous = NULL;
  current = process->message_to_fire_on_interrupt;
  while (current != NULL) {
    struct MessageToFireOnInterrupt* next = current->next_message_for_process;
    if (current->interrupt_number == interrupt_number &&
        current->message_id == message_id) {
      // We found the message!
      if (previous == NULL) {
        process->message_to_fire_on_interrupt = next;
      } else {
        previous->next_message_for_process = next;
      }
      free(current);
    } else {
      previous = current;
    }

    current = next;
  }
}

// Unregisters all messages for a process.
void UnregisterAllMessagesToForOnInterruptForProcess(struct Process* process) {
  while (process->message_to_fire_on_interrupt != NULL) {
    // Remove this message from the process's list.
    struct MessageToFireOnInterrupt* message =
        process->message_to_fire_on_interrupt;
    process->message_to_fire_on_interrupt = message->next_message_for_process;

    // Remove this message for the interrupt's list.
    int interrupt_number = message->interrupt_number & 0xF;
    struct MessageToFireOnInterrupt* previous = NULL;
    struct MessageToFireOnInterrupt* current =
        messages_to_fire_on_interrupt[interrupt_number];

    while (current != NULL) {
      struct MessageToFireOnInterrupt* next =
          current->next_message_for_interrupt;
      if (current == message) {
        // We found the message!
        if (previous == NULL) {
          messages_to_fire_on_interrupt[interrupt_number] = next;
        } else {
          previous->next_message_for_interrupt = next;
        }
      } else {
        previous = current;
      }
      current = next;
    }

    free(message);
  }
}

// The common handler that is called when a hardware interrupt occurs.
void CommonHardwareInterruptHandler(int interrupt_number) {
  if (interrupt_number == 0) {
    // The only hardware interrupt the microkernel knows about - the timer.
    TimerHandler();
  } else {
    // Send messages to any processes listening for this interrupt.
    struct MessageToFireOnInterrupt* message =
        messages_to_fire_on_interrupt[interrupt_number];
    while (message != NULL) {
      SendKernelMessageToProcess(message->process, message->message_id, 0, 0, 0,
                                 0, 0);
      message = message->next_message_for_interrupt;
    }

    // If the IDT entry that was invoked was greater than 40 (IRQ 8-15) we need
    // to send an EOI to the slave controller.
    if (interrupt_number >= 8) {
      outportb(0xA0, 0x20);
    }
  }

  // Send an EOI to the master interrupt controllerr.
  outportb(0x20, 0x20);

  // Interrupt could have awoken a thread when the system was currently halted.
  // If so, let's jump straight into it upon return.
  ScheduleThreadIfWeAreHalted();
}
