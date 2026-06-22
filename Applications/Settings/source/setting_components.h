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

#include <memory>
#include <string>
#include <vector>

#include "perception/registry.h"
#include "perception/ui/node.h"
#include "schema.h"

float GetTableColumnWidth(SettingType type);

std::shared_ptr<::perception::ui::Node> BuildTableCellComponent(
    ::perception::RegistryCorpus corpus, const std::string& ns_name,
    const std::string& key, int row_idx, int col_idx,
    const TableColumn& col,
    const ::perception::serialization::Value& cell_val);

std::shared_ptr<::perception::ui::Node> BuildSettingComponent(
    ::perception::RegistryCorpus corpus, const std::string& ns_name,
    const std::string& key, const std::string& change_key,
    const ActiveSetting& setting,
    const ::perception::serialization::Value& display_val);

std::shared_ptr<::perception::ui::Node> BuildTableSetting(
    ::perception::RegistryCorpus corpus, const std::string& ns_name,
    const std::string& key, const std::string& change_key,
    const ActiveSetting& setting);
