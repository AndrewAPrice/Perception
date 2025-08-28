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
#include "interrupts.asm.h"
#include "io.h"
#include "liballoc.h"
#include "messages.h"
#include "physical_allocator.h"
#include "process.h"
#include "scheduler.h"
#include "timer.h"
#include "tss.h"
#include "virtual_address_space.h"
#include "virtual_allocator.h"

namespace {

// A list of messages pointers to the IRQ handlers.
LinkedList<MessageToFireOnInterrupt,
           &MessageToFireOnInterrupt::node_in_interrupt>
    messages_to_fire_on_interrupt[16];

// Remaps hardware interrupts 0->15 to 32->47 on the Interrupt Descriptor Table
// to not overlap with CPU exceptions.
void RemapIrqsToNotOverlapWithCpuExceptions() {
  WriteIOByte(0x20, 0x11);
  WriteIOByte(0xA0, 0x11);
  WriteIOByte(0x21, 0x20);
  WriteIOByte(0xA1, 0x28);
  WriteIOByte(0x21, 0x04);
  WriteIOByte(0xA1, 0x02);
  WriteIOByte(0x21, 0x01);
  WriteIOByte(0xA1, 0x01);
  WriteIOByte(0x21, 0x0);
  WriteIOByte(0xA1, 0x0);
}

// Registers the 16 hardware interrupt handlers.
void RegisterInterruptHandlers() {
  RemapIrqsToNotOverlapWithCpuExceptions();

#ifndef __TEST__
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
#endif
}

// Allocates a stack to use for interrupts.
void AllocateInterruptStack() {
  size_t virtual_addr = KernelAddressSpace().AllocatePages(1);
  interrupt_stack_top = virtual_addr + PAGE_SIZE;

  SetInterruptStack(virtual_addr);
}

void HandleInterruptMessage(MessageToFireOnInterrupt& message_to_fire) {
  switch (message_to_fire.method) {
    case 0:
      SendKernelMessageToProcess(message_to_fire.process,
                                 message_to_fire.message_id, 0, 0, 0, 0, 0);
      break;
    case 1: {
      auto& params =
          message_to_fire.interrupt_poll_status_against_mask_read_port_params;

      // Can send up to 5 64-bit values in a message to the process.
      size_t longs_to_send[5];
      uint8* bytes_to_send = reinterpret_cast<uint8*>(longs_to_send);

      // Clear the message.
      for (size_t i = 0; i < 5; i++) longs_to_send[i] = 0;
      int bytes_read = 0;

      // Keep reading the status until the mask no longer matches.
      while (true) {
        uint8 status = ReadIOByte(params.status_port);
        if ((status & params.status_mask) != params.status_mask)
          break;  // Status doesn't match mask, stop reading.

        // Set the status and read a byte.
        bytes_to_send[bytes_read++] = status;
        bytes_to_send[bytes_read++] = ReadIOByte(params.read_port);

        // Check if the buffer has been filled up.
        if (bytes_read >= sizeof(longs_to_send)) {
          // Send the buffer to the process.
          SendKernelMessageToProcess(
              message_to_fire.process, message_to_fire.message_id,
              longs_to_send[0], longs_to_send[1], longs_to_send[2],
              longs_to_send[3], longs_to_send[4]);

          // Clear the buffer and reset the counter.
          for (size_t i = 0; i < 5; i++) longs_to_send[i] = 0;
          bytes_read = 0;
        }
      }

      // The status stopped matching the mask, send any bytes if they were read.
      if (bytes_read > 0) {
        SendKernelMessageToProcess(message_to_fire.process,
                                   message_to_fire.message_id, longs_to_send[0],
                                   longs_to_send[1], longs_to_send[2],
                                   longs_to_send[3], longs_to_send[4]);
      }
    } break;
  }
}

}  // namespace

// The top of the interrupt's stack.
size_t interrupt_stack_top;

// Initializes interrupts.
void InitializeInterrupts() {
  InitializeIdt();
  AllocateInterruptStack();

  for (int i = 0; i < 16; i++) {
    new (&messages_to_fire_on_interrupt[i])
        LinkedList<MessageToFireOnInterrupt,
                   &MessageToFireOnInterrupt::node_in_interrupt>();
  }

  // There are two sets of interrupts - CPU exceptions and hardware signals.
  // Handlers will be registered for both.
  RegisterExceptionInterrupts();
  RegisterInterruptHandlers();
}

// Registers a message to send to a process upon receiving an interrupt.
void RegisterMessageToSendOnInterrupt(size_t interrupt_number, Process* process,
                                      size_t message_id, size_t method,
                                      size_t param_1) {
  // Only drivers can listen to interrupts.
  if (!process->is_driver) return;
  interrupt_number &= 0xF;

  MessageToFireOnInterrupt* message =
      ObjectPool<MessageToFireOnInterrupt>::Allocate();
  message->process = process;
  message->message_id = message_id;
  message->interrupt_number = (uint8)interrupt_number;
  message->method = method;
  switch (method) {
    default:
      // Unknown method.
      ObjectPool<MessageToFireOnInterrupt>::Release(message);
      return;
    case 0:
      // Do nothing since this method needs no parameters.
      break;
    case 1: {
      auto& params =
          message->interrupt_poll_status_against_mask_read_port_params;
      params.status_port = static_cast<uint16>(param_1 & 0xFFFF);
      params.read_port = static_cast<uint16>((param_1 >> 16) & 0xFFFF);
      params.status_mask = static_cast<uint8>((param_1 >> 32) & 0xFF);

      if (params.status_mask == 0) {
        // Mask have a non-0 status mask.
        ObjectPool<MessageToFireOnInterrupt>::Release(message);
        return;
      }
      break;
    }
  }

  // Add to the linked list of messages for this interrupt and process.
  messages_to_fire_on_interrupt[interrupt_number].AddBack(message);
  process->messages_to_fire_on_interrupt.AddBack(message);
}

// Unregisters a message to send to a process upon receiving an interrupt.
void UnregisterMessageToSendOnInterrupt(size_t interrupt_number,
                                        Process* process, size_t message_id) {
  // Only drivers can listen to interrupts.
  if (!process->is_driver) return;
  interrupt_number &= 0xF;

  // Remove all matching messages from the interrupt's list.
  for (MessageToFireOnInterrupt* message :
       messages_to_fire_on_interrupt[interrupt_number]) {
    if (message->process == process && message->message_id == message_id) {
      // Found the message.
      messages_to_fire_on_interrupt[interrupt_number].Remove(message);
      message->process->messages_to_fire_on_interrupt.Remove(message);
      ObjectPool<MessageToFireOnInterrupt>::Release(message);
    }
  }
}

// Unregisters all messages for a process.
void UnregisterAllMessagesToForOnInterruptForProcess(Process* process) {
  while (auto* message = process->messages_to_fire_on_interrupt.PopFront()) {
    // Remove this message for the interrupt's list.
    int interrupt_number = message->interrupt_number & 0xF;
    messages_to_fire_on_interrupt[interrupt_number].Remove(message);
    ObjectPool<MessageToFireOnInterrupt>::Release(message);
  }
}

// The common handler that is called when a hardware interrupt occurs.
extern "C" void CommonHardwareInterruptHandler(int interrupt_number) {
  if (interrupt_number == 0) {
    // The only hardware interrupt the microkernel knows about - the timer.
    TimerHandler();
  } else {
    // Send messages to any processes listening for this interrupt.
    for (MessageToFireOnInterrupt* message :
         messages_to_fire_on_interrupt[interrupt_number]) {
      HandleInterruptMessage(*message);
    }

    // If the IDT entry that was invoked was greater than 40 (IRQ 8-15) an EOI
    // needs to be sent to the slave controller.
    if (interrupt_number >= 8) WriteIOByte(0xA0, 0x20);
  }

  // Send an EOI to the master interrupt controller.
  WriteIOByte(0x20, 0x20);

  // Interrupt could have awoken a thread when the system was currently halted.
  // If so, jump straight into the thread upon return.
  ScheduleThreadIfWeAreHalted();
}
