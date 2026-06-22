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

#include <functional>
#include <map>
#include <string>

#include "perception/registry.h"
#include "schema.h"

struct StagedChange {
  ::perception::RegistryCorpus corpus;
  std::string ns_name;
  std::string key;
  ::perception::serialization::Value value;
};

extern std::map<std::string, StagedChange> staged_changes;
extern std::map<std::string, ::perception::serialization::Value> original_values;

std::string RegistryValueToString(const ::perception::serialization::Value& val);

void StageChange(::perception::RegistryCorpus corpus, const std::string& ns_name,
                 const std::string& key, const ::perception::serialization::Value& val,
                 const ::perception::serialization::Value& original_val);

void UpdateTableCell(::perception::RegistryCorpus corpus, const std::string& ns_name,
                     const std::string& key, int row_idx, int col_idx,
                     const ::perception::serialization::Value& cell_val);

void AddTableRow(::perception::RegistryCorpus corpus, const std::string& ns_name,
                 const std::string& key, const ActiveSetting& setting,
                 std::function<void()> on_update = nullptr);

void RemoveTableRow(::perception::RegistryCorpus corpus, const std::string& ns_name,
                    const std::string& key, int row_idx,
                    std::function<void()> on_update = nullptr);

void ApplyStagedChanges();
void RevertStagedChanges();
