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

#include "perception/interrupts.h"

namespace perception {

// Registers a message to send to this process upon receiving an interrupt.
void RegisterMessageToSendOnInterrupt(uint8 interrupt, size_t message_id) {
	volatile register size_t syscall asm ("rdi") = 20;
	volatile register size_t interrupt_r asm ("rax") = (size_t)interrupt;
	volatile register size_t message_id_r asm ("rbx") = message_id;

	__asm__ __volatile__ ("syscall\n"::
		"r" (syscall), "r"(interrupt_r), "r"(message_id_r):"rcx", "r11");
}

// Unregisters a message to send to this process upon receiving an interrupt.
void UnregisterMessageToSendOnInterrupt(uint8 interrupt, size_t message_id) {
	volatile register size_t syscall asm ("rdi") = 21;
	volatile register size_t interrupt_r asm ("rax") = (size_t)interrupt;
	volatile register size_t message_id_r asm ("rbx") = message_id;

	__asm__ __volatile__ ("syscall\n"::
		"r" (syscall), "r"(interrupt_r), "r"(message_id_r):"rcx", "r11");
}

}
