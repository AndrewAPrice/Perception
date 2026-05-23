// Copyright 2024 Google LLC
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

#include "processes_tab.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "perception/fibers.h"
#include "perception/memory.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/time.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/table.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/text_alignment.h"
#include "process_details_window.h"
#include "process_tracking.h"

using ::perception::ForEachProcess;
using ::perception::GetProcessName;
using ::perception::MessageId;
using ::perception::ProcessId;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::CellHighlightability;
using ::perception::ui::components::Container;
using ::perception::ui::components::Label;
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::Table;
using ::perception::ui::components::UiWindow;

namespace {

class ProcessesDataSource : public Table::DataSource {
 public:
  ProcessesDataSource() : sorted_column_index_(-1), sort_ascending_(true) {}

  int GetNumberOfRows() override { return static_cast<int>(processes_.size()); }

  std::string GetCellValue(int row_index, int column_index) override {
    if (row_index < 0 || row_index >= static_cast<int>(processes_.size()))
      return "";

    const auto& proc = processes_[row_index];
    switch (column_index) {
      case 0:
        return proc.name;
      case 1:
        return std::to_string(proc.pid);
      case 2:
        return std::to_string(proc.registered_services);
      case 3:
        return std::to_string(proc.memory_used / 1024) + " KB";
      case 4:
        return std::to_string(((int)proc.cpu_percentage * 100) / 255) + "%";
      default:
        return "";
    }
  }

  void SortByColumn(int column_index, bool ascending) override {
    sorted_column_index_ = column_index;
    sort_ascending_ = ascending;
    SortData();
  }

  CellHighlightability GetCellHighlightablity(int row_index,
                                              int column_index) override {
    return CellHighlightability::RowHighlightable;
  }

  ProcessId GetProcessId(int row_index) {
    if (row_index < 0 || row_index >= static_cast<int>(processes_.size()))
      return 0;
    return processes_[row_index].pid;
  }

  void UpdateProcesses() {
    std::vector<ProcessInfo> new_processes;
    std::set<ProcessId> active_pids;

    ForEachProcess([&](ProcessId pid) {
      std::string name;
      auto cache_itr = process_names_cache_.find(pid);
      if (cache_itr != process_names_cache_.end()) {
        name = cache_itr->second;
      } else {
        name = GetProcessName(pid);
        if (name.empty()) return;
        process_names_cache_[pid] = name;
      }
      active_pids.insert(pid);

      size_t memory_used = 0;
      size_t creation_timestamp = 0;
      size_t registered_services = 0;
      uint8 cpu_percentages[8] = {0};
      perception::GetProcessHealthMetrics(pid, memory_used, creation_timestamp,
                                          registered_services, cpu_percentages);

      new_processes.push_back({.name = std::move(name),
                               .pid = pid,
                               .registered_services = registered_services,
                               .memory_used = memory_used,
                               .cpu_percentage = cpu_percentages[0]});
    });

    // Clean up terminated processes from name cache
    for (auto itr = process_names_cache_.begin();
         itr != process_names_cache_.end();) {
      if (active_pids.find(itr->first) == active_pids.end()) {
        itr = process_names_cache_.erase(itr);
      } else {
        ++itr;
      }
    }

    processes_ = std::move(new_processes);
    SortData();
  }

 private:
  struct ProcessInfo {
    std::string name;
    ProcessId pid;
    size_t registered_services;
    size_t memory_used;
    uint8 cpu_percentage;
  };

  std::vector<ProcessInfo> processes_;
  int sorted_column_index_;
  bool sort_ascending_;
  std::map<ProcessId, std::string> process_names_cache_;

  void SortData() {
    if (sorted_column_index_ < 0) return;

    std::sort(processes_.begin(), processes_.end(),
              [this](const ProcessInfo& a, const ProcessInfo& b) {
                if (sort_ascending_) {
                  switch (sorted_column_index_) {
                    case 0:  // Name
                      return a.name < b.name;
                    case 1:  // PID
                      return a.pid < b.pid;
                    case 2:  // Services
                      return a.registered_services < b.registered_services;
                    case 3:  // Memory
                      return a.memory_used < b.memory_used;
                    case 4:  // CPU
                      return a.cpu_percentage < b.cpu_percentage;
                    default:
                      return false;
                  }
                } else {
                  switch (sorted_column_index_) {
                    case 0:  // Name
                      return a.name > b.name;
                    case 1:  // PID
                      return a.pid > b.pid;
                    case 2:  // Services
                      return a.registered_services > b.registered_services;
                    case 3:  // Memory
                      return a.memory_used > b.memory_used;
                    case 4:  // CPU
                      return a.cpu_percentage > b.cpu_percentage;
                    default:
                      return false;
                  }
                }
              });
  }
};

// Widget containing the processes tab.
std::shared_ptr<Node> processes_tab;

// Data source of processes.
std::shared_ptr<ProcessesDataSource> processes_data_source;

// The table component pointer.
std::shared_ptr<Table> processes_table;

// Track tab visibility to optimize CPU tracking and query fiber.
bool processes_tab_visible = false;

}  // namespace

bool IsProcessesTabVisible() { return processes_tab_visible; }

void RebuildRunningProcesses() {
  if (processes_data_source && processes_table) {
    processes_data_source->UpdateProcesses();
    processes_table->Refresh();
  }
}

// Gets or constructs the processes tab of the launcher.
std::shared_ptr<Node> GetOrConstructProcessesTab() {
  if (!processes_data_source) {
    processes_data_source = std::make_shared<ProcessesDataSource>();
    processes_data_source->UpdateProcesses();
  }

  if (processes_tab) return processes_tab;

  std::vector<Table::Column> columns = {
      {.title = "Process Name",
       .layout_modifier =
           [](Layout& layout) {
             layout.SetFlexGrow(1.0f);
             layout.SetFlexShrink(1.0f);
             layout.SetWidth(100.0f);
           }},
      {.title = "PID",
       .layout_modifier = [](Layout& layout) { layout.SetWidth(40.0f); }},
      {.title = "Services",
       .layout_modifier = [](Layout& layout) { layout.SetWidth(60.0f); }},
      {.title = "Memory",
       .layout_modifier = [](Layout& layout) { layout.SetWidth(80.0f); }},
      {.title = "CPU usage",
       .layout_modifier = [](Layout& layout) { layout.SetWidth(80.0f); }}};

  processes_tab = Table::BasicTable(
      processes_data_source, columns,
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetMargin(YGEdgeAll, 12.0f);
      },
      &processes_table);

  processes_table->OnCellSelect([](int r, int c) {
    if (processes_data_source) {
      ProcessId pid = processes_data_source->GetProcessId(r);
      if (pid != 0) {
        OpenProcessDetailsWindow(pid);
      }
    }
  });

  return processes_tab;
}

void SetProcessesTabVisible(bool visible) {
  if (processes_tab_visible == visible) return;
  processes_tab_visible = visible;

  if (visible) {
    IncrementCpuTracking();
    // Trigger an immediate update of process info when switching back
    ::perception::Defer([]() { RebuildRunningProcesses(); });
    StartQueryFiber();
  } else {
    DecrementCpuTracking();
    // Reset tab pointers to guarantee clean reconstruction on visibility
    processes_tab.reset();
    processes_table.reset();
  }
}