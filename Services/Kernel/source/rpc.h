// Copyright 2026 Google LLC
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

#include "aa_tree.h"
#include "linked_list.h"

struct Process;

// An in-progress remote procedure call, where the caller is expecting a
// response.
struct RPC {
  // The caller who is expecting a response.
  Process* caller;

  // Node in the caller.
  LinkedListNode node_in_caller;

  // The callee who is expected to respond to the caller.
  Process* callee;

  // Node in the callee, in a tree keyed by synthetic_response_message_id.
  AATreeNode node_in_callee;

  // The message ID the caller expects the response to be.
  size_t response_message_id;

  // The synthetic message ID that the callee will send to respond to this RPC.
  // This is unique to the callee so there are no conflicting keys in
  // rpcs_waiting_on_this_process for fast lookup.
  size_t synthetic_response_message_id;
};
