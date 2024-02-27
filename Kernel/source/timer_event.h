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

#pragma once

#include "linked_list.h"
#include "types.h"

struct Process;

// An event that occurs at a timestamp.
struct TimerEvent {
  // The process to send this message to.
  Process* process_to_send_message_to;

  // The timestamp (in microseconds since the kernel started) to trigger
  // this event.
  size_t timestamp_to_trigger_at;

  // The message ID of the timer to send.
  size_t message_id_to_send;

  // Linked list, ordered by timerstamp_to_trigger, of all
  // scheduled TimerEvents.
  LinkedListNode node_in_all_timer_events;

  // Linked list in the process.
  LinkedListNode node_in_process;
};
