// Copyright 2021 Google LLC
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

#include <vector>
#include <iostream>

#include "ata.h"
#include "perception/fibers.h"
#include "perception/interrupts.h"
#include "perception/port_io.h"
#include "perception/scheduler.h"

using ::perception::Fiber;
using ::perception::GetCurrentlyExecutingFiber;
using ::perception::Read8BitsFromPort;
using ::perception::RegisterInterruptHandler;
using ::perception::Sleep;

namespace {

int kPrimaryInterrupt = 14;
int kSecondaryInterrupt = 15;

bool* primary_interrupt_triggered_ptr = nullptr;
bool* secondary_interrupt_triggered_ptr = nullptr;

std::atomic<Fiber*>* primary_waiter = nullptr;
std::atomic<Fiber*>* secondary_waiter = nullptr;

void CommonInterruptHandler(uint16 bus, bool& interrupt_triggered,
                            std::atomic<Fiber*>& waiting_on_interrupt) {
  if (waiting_on_interrupt.load(std::memory_order_relaxed) == nullptr) {
    // Acknowledge the interrupt on the IDE controller.
    (void)Read8BitsFromPort(ATA_COMMAND(bus));
  }

  if (interrupt_triggered) {
    // Interrupt already triggered but nothing has handled us
    // yet.
    return;
  }

  interrupt_triggered = true;

  Fiber* fiber =
      waiting_on_interrupt.exchange(nullptr, std::memory_order_acq_rel);
  if (fiber != nullptr) {
    fiber->WakeUp();
  }
}

void PrimaryInterruptHandler() {
  if (primary_interrupt_triggered_ptr != nullptr) {
    CommonInterruptHandler(ATA_BUS_PRIMARY, *primary_interrupt_triggered_ptr,
                           *primary_waiter);
  }
}

void SecondaryInterruptHandler() {
  if (secondary_interrupt_triggered_ptr != nullptr) {
    CommonInterruptHandler(ATA_BUS_SECONDARY,
                           *secondary_interrupt_triggered_ptr,
                           *secondary_waiter);
  }
}

}  // namespace

void ResetInterrupt(std::atomic<Fiber*>& waiting_on_interrupt,
                    bool& interrupt_triggered) {
  while (!interrupt_triggered) {
    // Someone else is already waiting on this interrupt.
    // Reset after the interrupt is called.
    waiting_on_interrupt.store(GetCurrentlyExecutingFiber(),
                               std::memory_order_release);
    Sleep();
  }
  interrupt_triggered = false;
}

void WaitForInterrupt(std::atomic<Fiber*>& waiting_on_interrupt,
                      bool& interrupt_triggered) {
  if (interrupt_triggered) {
    // Interrupt has already triggered.
    return;
  }

  waiting_on_interrupt.store(GetCurrentlyExecutingFiber(),
                             std::memory_order_release);
  Sleep();
}

void InitializeInterrupts(std::atomic<Fiber*>& waiting_primary,
                          bool& primary_triggered,
                          std::atomic<Fiber*>& waiting_secondary,
                          bool& secondary_triggered) {
  primary_interrupt_triggered_ptr = &primary_triggered;
  secondary_interrupt_triggered_ptr = &secondary_triggered;
  *primary_interrupt_triggered_ptr = true;
  *secondary_interrupt_triggered_ptr = true;
  primary_waiter = &waiting_primary;
  secondary_waiter = &waiting_secondary;

  // Listen to the interrupts.
  RegisterInterruptHandler(kPrimaryInterrupt, PrimaryInterruptHandler);
  RegisterInterruptHandler(kSecondaryInterrupt, SecondaryInterruptHandler);
}