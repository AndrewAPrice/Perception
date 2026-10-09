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

bool RegistryNamespace::SetValue(std::string_view key, const Value& value) {
  std::scoped_lock lock(mutex_);
  return GetOrCreateValueLocked(key).SetValue(value);
}

bool RegistryNamespace::SetDefaultValue(std::string_view key, const Value& value) {
  std::scoped_lock lock(mutex_);
  // A listener may have created an undefined placeholder before the default
  // was known, so placeholders are also filled in.
  RegistryValue& registry_value = GetOrCreateValueLocked(key);
  if (registry_value.GetValue().GetType() != Value::Type::UNDEFINED)
    return false;
  return registry_value.SetValue(value);
}

bool RegistryNamespace::DeleteValue(std::string_view key) {
  std::unique_ptr<RegistryValue> deleted_value;
  {
    std::scoped_lock lock(mutex_);
    auto it = values_.find(key);
    if (it == values_.end()) return false;
    deleted_value = std::move(it->second);
    values_.erase(it);
  }
  if (deleted_value) deleted_value->NotifyListeners();
  return true;
}

void RegistryNamespace::MarkReadOnly(std::string_view key) {
  std::scoped_lock lock(mutex_);
  GetOrCreateValueLocked(key).SetReadOnly(true);
}

void RegistryNamespace::MarkReadOnlyInstancePattern(std::string_view prefix,
                                                    std::string_view suffix) {
  std::scoped_lock lock(mutex_);
  std::string norm_prefix(prefix);
  if (!norm_prefix.empty() && norm_prefix.back() != '/')
    norm_prefix.push_back('/');
  std::string suffix_with_slash = "/" + std::string(suffix);
  for (const auto& pattern : read_only_patterns_) {
    if (pattern.prefix == norm_prefix &&
        pattern.suffix_with_slash == suffix_with_slash)
      return;
  }
  read_only_patterns_.push_back(
      ReadOnlyInstancePattern{std::move(norm_prefix),
                              std::move(suffix_with_slash)});
}

bool RegistryNamespace::IsReadOnly(std::string_view key) {
  std::scoped_lock lock(mutex_);
  return IsReadOnlyLocked(key);
}

bool RegistryNamespace::IsReadOnlyLocked(std::string_view key) const {
  auto it = values_.find(key);
  if (it != values_.end() && it->second->IsReadOnly()) return true;

  for (const auto& pattern : read_only_patterns_) {
    if (key.size() <=
        pattern.prefix.size() + pattern.suffix_with_slash.size())
      continue;
    if (!key.starts_with(pattern.prefix) ||
        !key.ends_with(pattern.suffix_with_slash))
      continue;
    std::string_view instance_id = key.substr(
        pattern.prefix.size(),
        key.size() - pattern.prefix.size() - pattern.suffix_with_slash.size());
    if (!instance_id.empty() &&
        instance_id.find('/') == std::string_view::npos)
      return true;
  }
  return false;
}

std::vector<std::string> RegistryNamespace::GetKeys() {
  std::scoped_lock lock(mutex_);
  std::vector<std::string> keys;
  for (const auto& pair : values_) {
    if (pair.second->GetValue().GetType() != Value::Type::UNDEFINED)
      keys.push_back(pair.first);
  }
  return keys;
}

std::vector<std::pair<std::string, Value>>
RegistryNamespace::GetPersistentValues() {
  std::scoped_lock lock(mutex_);
  std::vector<std::pair<std::string, Value>> result;
  for (const auto& [key, reg_val] : values_) {
    if (reg_val->GetValue().GetType() == Value::Type::UNDEFINED) continue;
    if (IsReadOnlyLocked(key)) continue;
    result.emplace_back(key, reg_val->GetValue());
  }
  return result;
}

void RegistryNamespace::RegisterListener(std::string_view key,
                                         ProcessId process_id,
                                         MessageId message_id) {
  std::scoped_lock lock(mutex_);
  GetOrCreateValueLocked(key).RegisterListener(process_id, message_id);
}

RegistryValue& RegistryNamespace::GetOrCreateValueLocked(std::string_view key) {
  auto& ptr = values_[std::string(key)];
  if (!ptr) ptr = std::make_unique<RegistryValue>();
  return *ptr;
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
