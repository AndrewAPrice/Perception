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

#include "registry_namespace.h"

using ::perception::MessageId;
using ::perception::ProcessId;
using ::perception::RegistryCorpus;
using ::perception::Status;
using ::perception::serialization::Value;

RegistryNamespace::RegistryNamespace(RegistryCorpus corpus, std::string_view name)
    : corpus_(corpus), name_(name) {}

StatusOr<Value> RegistryNamespace::GetValue(std::string_view key) {
  std::scoped_lock lock(mutex_);
  auto it = values_.find(key);
  if (it != values_.end()) {
    return Value(it->second->GetValue());
  }
  return Status::FILE_NOT_FOUND;
}

void RegistryNamespace::SetValue(std::string_view key, const Value& value) {
  std::scoped_lock lock(mutex_);
  auto& ptr = values_[std::string(key)];
  if (!ptr) {
    ptr = std::make_unique<RegistryValue>();
  }
  ptr->SetValue(value);
}

bool RegistryNamespace::SetDefaultValue(std::string_view key, const Value& value) {
  std::scoped_lock lock(mutex_);
  auto it = values_.find(key);
  if (it == values_.end()) {
    auto ptr = std::make_unique<RegistryValue>();
    ptr->SetValue(value);
    values_[std::string(key)] = std::move(ptr);
    return true;
  }
  return false;
}

bool RegistryNamespace::DeleteValue(std::string_view key) {
  std::scoped_lock lock(mutex_);
  auto it = values_.find(key);
  if (it != values_.end()) {
    values_.erase(it);
    return true;
  }
  return false;
}

std::vector<std::string> RegistryNamespace::GetKeys() {
  std::scoped_lock lock(mutex_);
  std::vector<std::string> keys;
  for (const auto& pair : values_) {
    keys.push_back(pair.first);
  }
  return keys;
}

void RegistryNamespace::RegisterListener(std::string_view key,
                                         ProcessId process_id,
                                         MessageId message_id) {
  std::scoped_lock lock(mutex_);
  auto& ptr = values_[std::string(key)];
  if (!ptr) {
    ptr = std::make_unique<RegistryValue>();
  }
  ptr->RegisterListener(process_id, message_id);
}

void RegistryNamespace::NotifyListeners(std::string_view key) {
  RegistryValue* val_ptr = nullptr;
  {
    std::scoped_lock lock(mutex_);
    auto it = values_.find(key);
    if (it != values_.end()) {
      val_ptr = it->second.get();
    }
  }
  if (val_ptr) {
    val_ptr->NotifyListeners();
  }
}
