#ifndef TEST
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

#include "interrupts/interrupts.h"

#include "interrupts/exceptions.h"
#include "memory/heap_allocator.h"
#include "interrupts/idt.h"
#include "interrupts/interrupts.asm.h"
#include "hardware/io.h"
#include "hardware/lapic.h"
#include "hardware/tlb_shootdown.h"
#include "ipc/messages.h"
#include "scheduling/cpu_core.h"
#include "memory/physical_allocator.h"
#include "processes/process.h"
#include "scheduling/scheduler.h"
#include "containers/spinlock.h"
#include "output/text_terminal.h"
#include "scheduling/timer.h"
#include "memory/virtual_address_space.h"
#include "memory/virtual_allocator.h"

namespace interrupts {

using containers::InterruptSafeSpinlock;
using containers::InterruptSafeSpinlockGuard;
using containers::LinkedList;
using containers::ObjectPool;
using hardware::ReadIOByte;
using hardware::SendLapicEoi;
using hardware::WriteIOByte;
using hardware::kApicTimerInterruptVector;
using hardware::kRescheduleIpiInterruptVector;
using hardware::kTlbShootdownIpiInterruptVector;
using memory::TemporarilyMapPhysicalPages;
using output::print;
using processes::Process;
using scheduling::kKernelCodeSelector;
using scheduling::ScheduleNextThread;
using scheduling::ScheduleThreadIfWeAreHalted;
using scheduling::TimerHandler;

namespace {

// Spinlock protecting interrupt listener registrations.
InterruptSafeSpinlock g_interrupts_spinlock;

// Maximum number of interrupt registrations allowed per process.
constexpr size_t kMaxInterruptRegistrationsPerProcess = 16;

// IDT entry flags for 64-bit interrupt gates (Present, DPL 0).
constexpr uint8 kIdtInterruptGateFlags = 0x8E;

// Master PIC command IO port.
constexpr uint16 kPicMasterCommandPort = 0x20;

// Master PIC data IO port.
constexpr uint16 kPicMasterDataPort = 0x21;

// Slave PIC command IO port.
constexpr uint16 kPicSlaveCommandPort = 0xA0;

// Slave PIC data IO port.
constexpr uint16 kPicSlaveDataPort = 0xA1;

// PIC initialization command word 1 (ICW1_INIT | ICW1_ICW4).
constexpr uint8 kPicInitCommand = 0x11;

// Master PIC interrupt vector offset (remapped to IRQ 32).
constexpr uint8 kPicMasterVectorOffset = 0x20;

// Slave PIC interrupt vector offset (remapped to IRQ 40).
constexpr uint8 kPicSlaveVectorOffset = 0x28;

// Master PIC cascade connection to IRQ2.
constexpr uint8 kPicMasterCascadeIrq = 0x04;

// Slave PIC cascade identity (IRQ2).
constexpr uint8 kPicSlaveCascadeIdentity = 0x02;

// 8086/88 mode flag for ICW4.
constexpr uint8 kPic8086Mode = 0x01;

// Array of the 16 hardware IRQ interrupt handlers.
const size_t kIrqHandlers[16] = {
    reinterpret_cast<size_t>(irq0),  reinterpret_cast<size_t>(irq1),
    reinterpret_cast<size_t>(irq2),  reinterpret_cast<size_t>(irq3),
    reinterpret_cast<size_t>(irq4),  reinterpret_cast<size_t>(irq5),
    reinterpret_cast<size_t>(irq6),  reinterpret_cast<size_t>(irq7),
    reinterpret_cast<size_t>(irq8),  reinterpret_cast<size_t>(irq9),
    reinterpret_cast<size_t>(irq10), reinterpret_cast<size_t>(irq11),
    reinterpret_cast<size_t>(irq12), reinterpret_cast<size_t>(irq13),
    reinterpret_cast<size_t>(irq14), reinterpret_cast<size_t>(irq15)};

// The assembly stubs in interrupts.asm push a handler number relative to the
// first remapped hardware IRQ rather than the raw interrupt vector. These are
// the numbers CommonHardwareInterruptHandler dispatches on, derived from the
// vectors the IDT is actually populated with so that renumbering a vector
// cannot silently route an IPI to the wrong branch.
constexpr int kApicTimerHandlerNumber =
    kApicTimerInterruptVector - kPicMasterVectorOffset;
constexpr int kRescheduleIpiHandlerNumber =
    kRescheduleIpiInterruptVector - kPicMasterVectorOffset;
constexpr int kTlbShootdownIpiHandlerNumber =
    kTlbShootdownIpiInterruptVector - kPicMasterVectorOffset;

static_assert(kApicTimerHandlerNumber == 16,
              "apic_timer_interrupt in interrupts.asm pushes 16.");
static_assert(kRescheduleIpiHandlerNumber == 17,
              "reschedule_ipi_interrupt in interrupts.asm pushes 17.");
static_assert(kTlbShootdownIpiHandlerNumber == 18,
              "tlb_shootdown_ipi_interrupt in interrupts.asm pushes 18.");

// A list of messages pointers to the IRQ handlers.
LinkedList<MessageToFireOnInterrupt,
           &MessageToFireOnInterrupt::node_in_interrupt>
    g_messages_to_fire_on_interrupt[16];

// Remaps hardware interrupts 0->15 to 32->47 on the Interrupt Descriptor Table
// to not overlap with CPU exceptions.
void RemapIrqsToNotOverlapWithCpuExceptions() {
  WriteIOByte(kPicMasterCommandPort, kPicInitCommand);
  WriteIOByte(kPicSlaveCommandPort, kPicInitCommand);
  WriteIOByte(kPicMasterDataPort, kPicMasterVectorOffset);
  WriteIOByte(kPicSlaveDataPort, kPicSlaveVectorOffset);
  WriteIOByte(kPicMasterDataPort, kPicMasterCascadeIrq);
  WriteIOByte(kPicSlaveDataPort, kPicSlaveCascadeIdentity);
  WriteIOByte(kPicMasterDataPort, kPic8086Mode);
  WriteIOByte(kPicSlaveDataPort, kPic8086Mode);
  WriteIOByte(kPicMasterDataPort, 0x0);
  WriteIOByte(kPicSlaveDataPort, 0x0);
}

// Registers the 16 hardware interrupt handlers.
void RegisterInterruptHandlers() {
  RemapIrqsToNotOverlapWithCpuExceptions();

  for (size_t i = 0; i < 16; i++) {
    SetIdtEntry(static_cast<unsigned char>(32 + i), kIrqHandlers[i],
                kKernelCodeSelector, kIdtInterruptGateFlags);
  }
  SetIdtEntry(kApicTimerInterruptVector,
              reinterpret_cast<size_t>(apic_timer_interrupt),
              kKernelCodeSelector, kIdtInterruptGateFlags);
  SetIdtEntry(kRescheduleIpiInterruptVector,
              reinterpret_cast<size_t>(reschedule_ipi_interrupt),
              kKernelCodeSelector, kIdtInterruptGateFlags);
  SetIdtEntry(kTlbShootdownIpiInterruptVector,
              reinterpret_cast<size_t>(tlb_shootdown_ipi_interrupt),
              kKernelCodeSelector, kIdtInterruptGateFlags);
}

void HandleInterruptMessage(MessageToFireOnInterrupt& message_to_fire) {
  if (message_to_fire.process == nullptr || message_to_fire.process->is_dying)
    return;

  switch (message_to_fire.method) {

    case 0:
      ipc::SendKernelMessageToProcess(message_to_fire.process,
                                      message_to_fire.message_id, 0, 0, 0, 0,
                                      0);
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
      int iterations = 0;

      // Keep reading the status until the mask no longer matches.
      while (true) {
        if (++iterations > 1000) {
          print << "Kernel interrupt handler loop limit reached! status_port: "
                << (size_t)params.status_port
                << ", status_mask: " << (size_t)params.status_mask << "\n";
          break;
        }
        uint8 status = ReadIOByte(params.status_port);
        if ((status & params.status_mask) != params.status_mask)
          break;  // Status doesn't match mask, stop reading.

        // Set the status and read a byte.
        bytes_to_send[bytes_read++] = status;
        bytes_to_send[bytes_read++] = ReadIOByte(params.read_port);

        // Check if the buffer has been filled up.
        if (bytes_read >= sizeof(longs_to_send)) {
          // Send the buffer to the process.
          ipc::SendKernelMessageToProcess(
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
        ipc::SendKernelMessageToProcess(
            message_to_fire.process, message_to_fire.message_id,
            longs_to_send[0], longs_to_send[1], longs_to_send[2],
            longs_to_send[3], longs_to_send[4]);
      }
    } break;
    case 2: {
      if (message_to_fire.mmio_address != 0) {
        // Map physical MMIO page to safely read status byte across any process
        // CR3
        size_t phys_page = message_to_fire.mmio_address & ~0xFFFULL;
        size_t page_offset = message_to_fire.mmio_address & 0xFFF;
        void* virt_page = TemporarilyMapPhysicalPages(phys_page, 7);
        if (virt_page != nullptr) {
          uint8 isr = *reinterpret_cast<volatile uint8*>(
              reinterpret_cast<char*>(virt_page) + page_offset);
          if (isr == 0) {
            // Interrupt line was not for this device or already cleared
            break;
          }
        }
      }
      ipc::SendKernelMessageToProcess(message_to_fire.process,
                                      message_to_fire.message_id, 0, 0, 0, 0,
                                      0);
    } break;
  }
}

}  // namespace


// Initializes interrupts.
void InitializeInterrupts() {
  InitializeIdt();

  for (int i = 0; i < 16; i++) {
    new (&g_messages_to_fire_on_interrupt[i])
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
  if (interrupt_number >= 16) return;

  if (method == 2) {
    // MMIO physical address must be non-zero and above physical page 0
    if (param_1 == 0 || (param_1 & ~0xFFFULL) == 0) return;
  }

  {
    InterruptSafeSpinlockGuard guard(g_interrupts_spinlock);
    size_t count = 0;
    for (MessageToFireOnInterrupt* existing :
         process->messages_to_fire_on_interrupt) {
      if (existing->interrupt_number == interrupt_number &&
          existing->message_id == message_id) {
        // Already registered.
        return;
      }
      count++;
    }
    if (count >= kMaxInterruptRegistrationsPerProcess) return;
  }

  MessageToFireOnInterrupt* message =
      ObjectPool<MessageToFireOnInterrupt>::Allocate();
  if (message == nullptr) return;

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
    case 2:
      message->mmio_address = param_1;
      break;
  }

  // Add to the linked list of messages for this interrupt and process.
  {
    InterruptSafeSpinlockGuard guard(g_interrupts_spinlock);
    g_messages_to_fire_on_interrupt[interrupt_number].AddBack(message);
    process->messages_to_fire_on_interrupt.AddBack(message);
  }
}

// Unregisters a message to send to a process upon receiving an interrupt.
void UnregisterMessageToSendOnInterrupt(size_t interrupt_number,
                                        Process* process, size_t message_id) {
  // Only drivers can listen to interrupts.
  if (!process->is_driver) return;
  if (interrupt_number >= 16) return;

  InterruptSafeSpinlockGuard guard(g_interrupts_spinlock);
  // Remove all matching messages from the interrupt's list.
  auto* message = g_messages_to_fire_on_interrupt[interrupt_number].FirstItem();
  while (message != nullptr) {
    auto* next =
        g_messages_to_fire_on_interrupt[interrupt_number].NextItem(message);
    if (message->process == process && message->message_id == message_id) {
      // Found the message.
      g_messages_to_fire_on_interrupt[interrupt_number].Remove(message);
      message->process->messages_to_fire_on_interrupt.Remove(message);
      ObjectPool<MessageToFireOnInterrupt>::Release(message);
    }
    message = next;
  }
}

void UnregisterAllMessagesToFireOnInterruptForProcess(Process* process) {
  InterruptSafeSpinlockGuard guard(g_interrupts_spinlock);
  while (auto* message = process->messages_to_fire_on_interrupt.PopFront()) {
    // Remove this message for the interrupt's list.
    int interrupt_number = message->interrupt_number & 0xF;
    g_messages_to_fire_on_interrupt[interrupt_number].Remove(message);
    ObjectPool<MessageToFireOnInterrupt>::Release(message);
  }
}

// The common handler that is called when a hardware interrupt occurs.
extern "C" void CommonHardwareInterruptHandler(int interrupt_number) {
  if (interrupt_number == kApicTimerHandlerNumber) {
    // The Local APIC Timer interrupt.
    TimerHandler();
#ifndef TEST
    SendLapicEoi();
#endif
  } else if (interrupt_number == kRescheduleIpiHandlerNumber) {
    // Cross-core reschedule IPI.
#ifndef TEST
    SendLapicEoi();
#endif
    ScheduleNextThread();
  } else if (interrupt_number == kTlbShootdownIpiHandlerNumber) {
    // Cross-core TLB shootdown IPI. A CR3 reload would not evict the kernel's
    // global pages, so acknowledge through the shared shootdown protocol, which
    // invalidates the exact address the sender is waiting on.
#ifndef TEST
    SendLapicEoi();
    hardware::PollTlbShootdown();
#endif
  } else if (interrupt_number == 0) {
    // The legacy PIT periodic timer.
    TimerHandler();
    // Send an EOI to the master interrupt controller.
    WriteIOByte(0x20, 0x20);
  } else {
    // Send messages to any processes listening for this interrupt.
    {
      InterruptSafeSpinlockGuard guard(g_interrupts_spinlock);
      for (MessageToFireOnInterrupt* message :
           g_messages_to_fire_on_interrupt[interrupt_number]) {
        HandleInterruptMessage(*message);
      }
    }

    // If the IDT entry that was invoked was greater than 40 (IRQ 8-15) an EOI
    // needs to be sent to the slave controller.
    if (interrupt_number >= 8) WriteIOByte(0xA0, 0x20);

    // Send an EOI to the master interrupt controller.
    WriteIOByte(0x20, 0x20);
  }

  // Interrupt could have awoken a thread when the system was currently halted.
  // If so, jump straight into the thread upon return.
  ScheduleThreadIfWeAreHalted();
}

}  // namespace interrupts

#endif // TEST
