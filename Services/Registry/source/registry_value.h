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

#include <vector>

#include "perception/messages.h"
#include "perception/processes.h"
#include "perception/serialization/serializer.h"
#include "perception/serialization/value.h"

class RegistryValue {
 public:
  struct ListenerInfo {
    ::perception::ProcessId process_id;
    ::perception::MessageId message_id;
  };

  RegistryValue() = default;
  ~RegistryValue();

  const ::perception::serialization::Value& GetValue() const { return value_; }
  void SetValue(const ::perception::serialization::Value& value) { value_ = value; }

  // Registers a listener for this value.
  void RegisterListener(::perception::ProcessId process_id,
                        ::perception::MessageId message_id);

  // Removes a listener from this value (called from UnregisterListener).
  void RemoveListener(::perception::ProcessId process_id,
                      ::perception::MessageId message_id);

  // Notifies all listeners listening to this value.
  void NotifyListeners();

 private:
  ::perception::serialization::Value value_;
  std::vector<ListenerInfo> listeners_;
};

// Unregisters a listener globally.
void UnregisterListener(::perception::ProcessId process_id,
                        ::perception::MessageId message_id);
