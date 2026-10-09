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

#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

#include "perception/messages.h"
#include "perception/processes.h"
#include "perception/registry.h"
#include "perception/serialization/serializer.h"
#include "status.h"
#include "registry_value.h"

class RegistryNamespace {
 public:
  RegistryNamespace(::perception::RegistryCorpus corpus, std::string_view name);
  ~RegistryNamespace() = default;

  ::perception::RegistryCorpus GetCorpus() const { return corpus_; }
  std::string_view GetName() const { return name_; }

  // Retrieve a value.
  StatusOr<::perception::serialization::Value> GetValue(std::string_view key);

  // Set a value. Returns true if the value changed.
  bool SetValue(std::string_view key,
                const ::perception::serialization::Value& value);

  // Set default value if key doesn't already exist.
  // Returns true if the default value was set, false if the key already existed.
  bool SetDefaultValue(std::string_view key,
                       const ::perception::serialization::Value& value);

  // Delete a value. Returns true if the value existed.
  bool DeleteValue(std::string_view key);

  // Marks a key as read-only (volatile, excluded from disk persistence).
  void MarkReadOnly(std::string_view key);

  // Marks any key matching "<prefix><id>/<suffix>" as read-only (volatile).
  void MarkReadOnlyInstancePattern(std::string_view prefix,
                                   std::string_view suffix);

  // Returns whether a key is read-only.
  bool IsReadOnly(std::string_view key);

  // Get all keys.
  std::vector<std::string> GetKeys();

  // Returns all persistent (non-read-only, defined) key-value pairs.
  std::vector<std::pair<std::string, ::perception::serialization::Value>>
  GetPersistentValues();

  // Register a listener for a key.
  void RegisterListener(std::string_view key,
                        ::perception::ProcessId process_id,
                        ::perception::MessageId message_id);

  // Notify listeners watching a key.
  void NotifyListeners(std::string_view key);

 private:
  struct ReadOnlyInstancePattern {
    std::string prefix;
    std::string suffix_with_slash;
  };

  // Returns the value for a key, creating an empty value if it doesn't exist.
  // The caller must hold mutex_.
  RegistryValue& GetOrCreateValueLocked(std::string_view key);

  // Returns whether a key is read-only. The caller must hold mutex_.
  bool IsReadOnlyLocked(std::string_view key) const;

  ::perception::RegistryCorpus corpus_;
  std::string name_;
  std::mutex mutex_;
  std::map<std::string, std::unique_ptr<RegistryValue>, std::less<>> values_;
  std::vector<ReadOnlyInstancePattern> read_only_patterns_;
};
