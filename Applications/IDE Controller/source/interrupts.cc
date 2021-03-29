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

#include "ata.h"
#include "perception/port_io.h"
#include "perception/fibers.h"
#include "perception/interrupts.h"
#include "perception/scheduler.h"

using ::perception::Fiber;
using ::perception::RegisterInterruptHandler;
using ::perception::Sleep;
using ::perception::GetCurrentlyExecutingFiber;
using ::perception::Read8BitsFromPort;

namespace {

int kPrimaryInterrupt = 14;
int kSecondaryInterrupt = 15;

bool primary_interrupt_triggered;
bool secondary_interrupt_triggered;

std::vector<Fiber*> waiting_on_primary_interrupt;
std::vector<Fiber*> waiting_on_secondary_interrupt;

bool GetInterruptNumber(bool primary_bus){
	return primary_bus ? kPrimaryInterrupt : kSecondaryInterrupt;
}

void CommonInterruptHandler(uint16 bus,
	bool& interrupt_triggered,
	std::vector<Fiber*>& waiting_on_interrupt) {

	// Read the status bit.
//	(void)Read8BitsFromPort(ATA_COMMAND(bus));
	/*uint8 status;
	if (!((status = Read8BitsFromPort(ATA_COMMAND(bus))) & 0x8)
		&& !(status & 0x1)) {
		// Device isn't ready, so the interrupt might not be from
		// this device.
		std::cout << "Device not ready" << std::endl;
		return;
	}*/

	if (interrupt_triggered) {
		// Interrupt already triggered but nothing has handled us
		// yet.
		return;
	}

	interrupt_triggered = true;

	// Wake up each sleeping fiber. Iterating over the vector of
	// fibers should be fiber safe but not thread safe.
	for (Fiber* fiber : waiting_on_interrupt) {
		fiber->WakeUp();
	}

	waiting_on_interrupt.clear();
}

void PrimaryInterruptHandler() {
	CommonInterruptHandler(ATA_BUS_PRIMARY,
		primary_interrupt_triggered,
		waiting_on_primary_interrupt);
}

void SecondaryInterruptHandler() {
	CommonInterruptHandler(ATA_BUS_SECONDARY,
		secondary_interrupt_triggered,
		waiting_on_secondary_interrupt);
}

void CommonResetInterrupt(bool& interrupt_triggered,
	std::vector<Fiber*>& waiting_on_interrupt) {
	while (!interrupt_triggered) {
		// Someone else is already waiting on
		// this interrupt. Reset after the interrupt
		// is called.
		waiting_on_primary_interrupt.push_back(
			GetCurrentlyExecutingFiber());
		Sleep();
	}
	interrupt_triggered = false;
}

void CommonWaitForInterrupt(bool& interrupt_triggered,
	std::vector<Fiber*>& waiting_on_interrupt) {
	if (interrupt_triggered) {
		// Interrupt has already triggered.
		return;
	}

	waiting_on_interrupt.push_back(
		GetCurrentlyExecutingFiber());
	Sleep();
}

}

void ResetInterrupt(bool primary_bus) {
	if (primary_bus) {
		CommonResetInterrupt(primary_interrupt_triggered,
			waiting_on_primary_interrupt);
	} else {
		CommonResetInterrupt(secondary_interrupt_triggered,
			waiting_on_secondary_interrupt);
	}
}

void WaitForInterrupt(bool primary_bus) {
	if (primary_bus) {
		CommonWaitForInterrupt(primary_interrupt_triggered,
			waiting_on_primary_interrupt);
	} else {
		CommonWaitForInterrupt(secondary_interrupt_triggered,
			waiting_on_secondary_interrupt);
	}
}


void InitializeInterrupts() {
	primary_interrupt_triggered = true;
	secondary_interrupt_triggered = true;

	// Listen to the interrupts.
	RegisterInterruptHandler(kPrimaryInterrupt, PrimaryInterruptHandler);
	RegisterInterruptHandler(kSecondaryInterrupt, SecondaryInterruptHandler);
}