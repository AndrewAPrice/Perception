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

#pragma once

#include "linked_list.h"
#include "types.h"

// The number of interrupts.
#define NUMBER_OF_INTERRUPTS 16

struct Process;

// Parameters for interrupt method 1.
struct InterruptPollStatusAgainstMaskReadPortParams {
  // The port to read the status from.
  uint16 status_port;
  // The mask to compare the status against.
  uint8 status_mask;
  // The port to read the data from.
  uint16 read_port;
};

// A message to fire on an interrupt.
struct MessageToFireOnInterrupt {
  // The process to send the message to.
  Process* process;

  // The message ID to fire.
  size_t message_id;

  // The interrupt number.
  uint8 interrupt_number;

  // The method used for processing this interrupt.
  uint8 method;

  union {
    // Parameters when method is `1`.
    InterruptPollStatusAgainstMaskReadPortParams interrupt_poll_status_against_mask_read_port_params;
  };

  // Node in the linked list.
  LinkedListNode node_in_interrupt;
  LinkedListNode node_in_process;
};

// The top of the interrupt's stack.
extern size_t interrupt_stack_top;

// Initializes interrupts.
void InitializeInterrupts();

// Registers a message to send to a process upon receiving an interrupt.
void RegisterMessageToSendOnInterrupt(size_t interrupt_number, Process* process,
                                      size_t message_id, size_t method,
                                      size_t param_1);

// Unregisters a message to send to a process upon receiving an interrupt.
void UnregisterMessageToSendOnInterrupt(size_t interrupt_number,
                                        Process* process, size_t message_id);

// Unregisters all messages for a process.
void UnregisterAllMessagesToForOnInterruptForProcess(Process* process);
