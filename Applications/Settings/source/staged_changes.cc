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

#include "staged_changes.h"

#include <cstdio>
#include <vector>

#include "perception/registry.h"
#include "settings_window.h"

using ::perception::RegistryCorpus;
using ::perception::SetRegistryValue;
using ::perception::serialization::Value;

std::map<std::string, StagedChange> staged_changes;
std::map<std::string, Value> original_values;

std::string RegistryValueToString(const Value& val) {
  switch (val.GetType()) {
    case Value::Type::BOOLEAN:
      return val.BoolValue().value_or(false) ? "true" : "false";
    case Value::Type::INTEGER:
      return std::to_string(val.IntegerValue().value_or(0));
    case Value::Type::FLOAT:
      return std::to_string(val.FloatValue().value_or(0.0));
    case Value::Type::STRING:
      return std::string(val.StringValue().value_or(""));
    case Value::Type::ARRAY: {
      auto* arr = val.ArrayValue();
      if (!arr) return "[]";
      std::string res = "[";
      for (size_t i = 0; i < arr->size(); ++i) {
        if (i > 0) res += ", ";
        res += RegistryValueToString((*arr)[i]);
      }
      res += "]";
      return res;
    }
    case Value::Type::COLOR_RGB: {
      auto color = val.ColorRGBValue().value_or(0);
      char buf[8];
      std::sprintf(buf, "%02x%02x%02x", (color >> 16) & 0xFF,
                   (color >> 8) & 0xFF, color & 0xFF);
      return std::string(buf);
    }
    default:
      return "";
  }
}

void StageChange(RegistryCorpus corpus, const std::string& ns_name,
                 const std::string& key, const Value& val,
                 const Value& original_val) {
  std::string change_key =
      (corpus == RegistryCorpus::APPLICATIONS ? "a:" : "l:") + ns_name + ":" +
      key;
  std::string val_str = RegistryValueToString(val);
  std::string orig_str = RegistryValueToString(original_val);

  if (val_str == orig_str) {
    staged_changes.erase(change_key);
  } else {
    staged_changes[change_key] = {corpus, ns_name, key, val};
  }
  UpdateButtonStates();
}

void UpdateTableCell(RegistryCorpus corpus, const std::string& ns_name,
                     const std::string& key, int row_idx, int col_idx,
                     const Value& cell_val) {
  std::string change_key =
      (corpus == RegistryCorpus::APPLICATIONS ? "a:" : "l:") + ns_name + ":" +
      key;

  Value current_val;
  auto staged_it = staged_changes.find(change_key);
  if (staged_it != staged_changes.end()) {
    current_val = staged_it->second.value;
  } else {
    current_val = original_values[change_key];
  }

  std::vector<Value> rows;
  if (current_val.GetType() == Value::Type::ARRAY) {
    if (auto* arr = current_val.ArrayValue()) {
      rows = *arr;
    }
  }

  if (row_idx >= 0 && row_idx < static_cast<int>(rows.size())) {
    std::vector<Value> row_cells;
    if (rows[row_idx].GetType() == Value::Type::ARRAY) {
      if (auto* arr = rows[row_idx].ArrayValue()) {
        row_cells = *arr;
      }
    }

    if (col_idx >= 0 && col_idx < static_cast<int>(row_cells.size())) {
      row_cells[col_idx] = cell_val;
      rows[row_idx] = Value(row_cells);
      StageChange(corpus, ns_name, key, Value(rows),
                  original_values[change_key]);
    }
  }
}

void AddTableRow(RegistryCorpus corpus, const std::string& ns_name,
                 const std::string& key, const ActiveSetting& setting,
                 std::function<void()> on_update) {
  std::string change_key =
      (corpus == RegistryCorpus::APPLICATIONS ? "a:" : "l:") + ns_name + ":" +
      key;

  Value current_val;
  auto staged_it = staged_changes.find(change_key);
  if (staged_it != staged_changes.end()) {
    current_val = staged_it->second.value;
  } else {
    current_val = original_values[change_key];
  }

  std::vector<Value> rows;
  if (current_val.GetType() == Value::Type::ARRAY) {
    if (auto* arr = current_val.ArrayValue()) {
      rows = *arr;
    }
  }

  std::vector<Value> new_row_cells;
  for (const auto& col : setting.table_columns) {
    new_row_cells.push_back(GetDefaultValueForColumnType(col.type));
  }

  rows.push_back(Value(new_row_cells));
  StageChange(corpus, ns_name, key, Value(rows), original_values[change_key]);
  if (on_update) {
    on_update();
  } else {
    RefreshRightPanel();
  }
}

void RemoveTableRow(RegistryCorpus corpus, const std::string& ns_name,
                    const std::string& key, int row_idx,
                    std::function<void()> on_update) {
  std::string change_key =
      (corpus == RegistryCorpus::APPLICATIONS ? "a:" : "l:") + ns_name + ":" +
      key;

  Value current_val;
  auto staged_it = staged_changes.find(change_key);
  if (staged_it != staged_changes.end()) {
    current_val = staged_it->second.value;
  } else {
    current_val = original_values[change_key];
  }

  std::vector<Value> rows;
  if (current_val.GetType() == Value::Type::ARRAY) {
    if (auto* arr = current_val.ArrayValue()) {
      rows = *arr;
    }
  }

  if (row_idx >= 0 && row_idx < static_cast<int>(rows.size())) {
    rows.erase(rows.begin() + row_idx);
    StageChange(corpus, ns_name, key, Value(rows), original_values[change_key]);
    if (on_update) {
      on_update();
    } else {
      RefreshRightPanel();
    }
  }
}

void ApplyStagedChanges() {
  if (staged_changes.empty()) return;
  for (const auto& [ck, change] : staged_changes) {
    SetRegistryValue(change.corpus, change.ns_name, change.key, change.value);
    original_values[ck] = change.value;
  }
  staged_changes.clear();
  RefreshRightPanel();
  UpdateButtonStates();
}

void RevertStagedChanges() {
  if (staged_changes.empty()) return;
  staged_changes.clear();
  RefreshRightPanel();
  UpdateButtonStates();
}
