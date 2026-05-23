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

#include "process_details_window.h"

#include <algorithm>
#include <map>
#include <memory>
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
#include "perception/ui/components/group_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/table.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/text_alignment.h"
#include "process_tracking.h"

using ::perception::MessageId;
using ::perception::ProcessId;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::CellHighlightability;
using ::perception::ui::components::Container;
using ::perception::ui::components::GroupBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::Table;
using ::perception::ui::components::UiWindow;

namespace {

class ServicesDataSource : public Table::DataSource {
 public:
  ServicesDataSource(ProcessId pid)
      : pid_(pid), sorted_column_index_(-1), sort_ascending_(true) {
    UpdateServices();
  }

  int GetNumberOfRows() override {
    return static_cast<int>(service_counts_.size());
  }

  std::string GetCellValue(int row_index, int column_index) override {
    if (row_index < 0 || row_index >= static_cast<int>(service_counts_.size()))
      return "";
    const auto& pair = service_counts_[row_index];
    if (column_index == 0) return pair.first;
    if (column_index == 1) return std::to_string(pair.second);
    return "";
  }

  void SortByColumn(int column_index, bool ascending) override {
    sorted_column_index_ = column_index;
    sort_ascending_ = ascending;
    SortData();
  }

  CellHighlightability GetCellHighlightablity(int row_index,
                                              int column_index) override {
    return CellHighlightability::NotHighlightable;
  }

  void UpdateServices() {
    std::map<std::string, int> counts;
    ::perception::ForEachService([&](ProcessId process_id,
                                     MessageId message_id) {
      if (process_id == pid_) {
        std::string name = ::perception::GetServiceName(process_id, message_id);
        counts[name]++;
      }
    });
    service_counts_.clear();
    for (const auto& pair : counts) {
      service_counts_.push_back(pair);
    }
    SortData();
  }

 private:
  ProcessId pid_;
  std::vector<std::pair<std::string, int>> service_counts_;
  int sorted_column_index_;
  bool sort_ascending_;

  void SortData() {
    if (sorted_column_index_ < 0) return;
    std::sort(service_counts_.begin(), service_counts_.end(),
              [this](const auto& a, const auto& b) {
                if (sort_ascending_) {
                  if (sorted_column_index_ == 0) {
                    return a.first < b.first;
                  } else {
                    return a.second < b.second;
                  }
                } else {
                  if (sorted_column_index_ == 0) {
                    return a.first > b.first;
                  } else {
                    return a.second > b.second;
                  }
                }
              });
  }
};

struct ProcessDetailsWindowInfo {
  std::shared_ptr<Node> window_node;
  std::shared_ptr<ServicesDataSource> services_data_source;
  std::shared_ptr<Table> services_table;
  std::shared_ptr<Label> uptime_label;
  std::shared_ptr<Label> name_label;
  std::shared_ptr<Label> memory_label;
  std::shared_ptr<Label> cpu_labels[8];
  size_t creation_timestamp;
};

std::map<ProcessId, ProcessDetailsWindowInfo> open_process_details_windows;

std::string FormatUptime(size_t uptime_micros) {
  size_t seconds = uptime_micros / 1000000;
  size_t minutes = seconds / 60;
  seconds %= 60;
  size_t hours = minutes / 60;
  minutes %= 60;
  size_t days = hours / 24;
  hours %= 24;

  std::string result = "";
  if (days > 0) {
    result += std::to_string(days) + "d ";
  }
  if (hours > 0 || days > 0) {
    result += std::to_string(hours) + "h ";
  }
  if (minutes > 0 || hours > 0 || days > 0) {
    result += std::to_string(minutes) + "m ";
  }
  result += std::to_string(seconds) + "s";
  return result;
}

}  // namespace

void OpenProcessDetailsWindow(ProcessId pid) {
  // 1. If there is already an open window for this PID, focus on it and return.
  auto itr = open_process_details_windows.find(pid);
  if (itr != open_process_details_windows.end()) {
    itr->second.window_node->Get<UiWindow>()->Focus();
    return;
  }

  // 2. Otherwise, let's get the initial process information.
  std::string proc_name = perception::GetProcessName(pid);
  if (proc_name.empty()) return;  // Process doesn't exist or name is empty.

  size_t memory_used = 0;
  size_t creation_timestamp = 0;
  size_t registered_services = 0;
  uint8 cpu_percentages[8] = {0};
  perception::GetProcessHealthMetrics(pid, memory_used, creation_timestamp,
                                      registered_services, cpu_percentages);

  // Increment CPU tracking ref count.
  IncrementCpuTracking();

  // 3. Let's build the details window UI.
  auto name_label_node = Label::BasicLabel(
      "Process: " + proc_name + " (PID: " + std::to_string(pid) + ")");
  name_label_node->Apply(
      [](Label& label) { label.SetColor(0xFF000000); },
      [](Layout& layout) { layout.SetMargin(YGEdgeBottom, 4.0f); });

  auto uptime_label_node = Label::BasicLabel("Uptime: ");
  uptime_label_node->Apply(
      [](Label& label) { label.SetColor(0xFF000000); },
      [](Layout& layout) { layout.SetMargin(YGEdgeBottom, 4.0f); });

  auto memory_label_node = Label::BasicLabel("Memory: ");
  memory_label_node->Apply(
      [](Label& label) { label.SetColor(0xFF000000); },
      [](Layout& layout) { layout.SetMargin(YGEdgeBottom, 8.0f); });

  // Create labels for the 8 cores.
  std::shared_ptr<Node> cpu_label_nodes[8];
  std::shared_ptr<Label> cpu_labels[8];

  for (int i = 0; i < 8; i++) {
    cpu_label_nodes[i] =
        Label::BasicLabel("Core " + std::to_string(i) + ": 0%");
    cpu_label_nodes[i]->Apply([](Label& label) { label.SetColor(0xFF000000); },
                              [](Layout& layout) {
                                layout.SetWidth(80.0f);
                                layout.SetMargin(YGEdgeBottom, 2.0f);
                              });
    cpu_labels[i] = cpu_label_nodes[i]->Get<Label>();
  }

  // Horizontal rows of CPU labels
  auto cpu_row1 = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetJustifyContent(YGJustifySpaceBetween);
      },
      cpu_label_nodes[0], cpu_label_nodes[1], cpu_label_nodes[2],
      cpu_label_nodes[3]);
  auto cpu_row2 = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetJustifyContent(YGJustifySpaceBetween);
      },
      cpu_label_nodes[4], cpu_label_nodes[5], cpu_label_nodes[6],
      cpu_label_nodes[7]);

  auto cpu_section = GroupBox::VerticalGroupBox(
      "CPU Usage per Core:",
      cpu_row1, cpu_row2);

  // Services Table columns
  std::vector<Table::Column> columns = {
      {.title = "Service Name",
       .layout_modifier =
           [](Layout& layout) {
             layout.SetFlexGrow(1.0f);
             layout.SetFlexShrink(1.0f);
             layout.SetWidth(180.0f);
           }},
      {.title = "Instances",
       .layout_modifier = [](Layout& layout) { layout.SetWidth(80.0f); }}};

  auto services_ds = std::make_shared<ServicesDataSource>(pid);
  std::shared_ptr<Table> services_table_component;
  auto services_table_node = Table::BasicTable(
      services_ds, columns,
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetMinHeight(120.0f);
        layout.SetMargin(YGEdgeBottom, 8.0f);
      },
      &services_table_component);

  // Terminate Button
  auto terminate_button_node = Button::TextButton(
      "Terminate Process", [pid]() { ::perception::TerminateProcesss(pid); },
      [](Button& button) { button.SetButtonStyle(Button::ButtonStyle::RED); },
      [](Layout& layout) {
        layout.SetAlignSelf(YGAlignFlexEnd);
        layout.SetPadding(YGEdgeHorizontal, 16.0f);
        layout.SetPadding(YGEdgeVertical, 6.0f);
      });

  auto content_container = Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetPadding(YGEdgeAll, 8.0f);
      },
      name_label_node, uptime_label_node, memory_label_node, cpu_section,
      Label::BasicLabel(
          "Registered Services:",
          [](Label& label) { label.SetColor(0xFF000000); },
          [](Layout& layout) { layout.SetMargin(YGEdgeBottom, 4.0f); }),
      services_table_node, terminate_button_node);

  std::shared_ptr<Node> window_node = UiWindow::ResizableWindowWithTitleBar(
      proc_name + " - Process Details",
      [pid](UiWindow& window) {
        window.OnClose([pid]() {
          perception::Defer([pid]() {
            auto itr = open_process_details_windows.find(pid);
            if (itr != open_process_details_windows.end()) {
              open_process_details_windows.erase(itr);
              DecrementCpuTracking();
            }
          });
        });
      },
      [](Layout& layout) {
        layout.SetWidth(450.0f);
        layout.SetHeight(420.0f);
      },
      content_container);

  ProcessDetailsWindowInfo info;
  info.window_node = window_node;
  info.services_data_source = services_ds;
  info.services_table = services_table_component;
  info.uptime_label = uptime_label_node->Get<Label>();
  info.name_label = name_label_node->Get<Label>();
  info.memory_label = memory_label_node->Get<Label>();
  for (int i = 0; i < 8; i++) {
    info.cpu_labels[i] = cpu_labels[i];
  }
  info.creation_timestamp = creation_timestamp;

  open_process_details_windows[pid] = info;

  StartQueryFiber();
}

bool AreAnyProcessDetailsWindowsOpen() {
  return !open_process_details_windows.empty();
}

void UpdateOpenProcessDetailsWindows() {
  std::vector<ProcessId> pids_to_close;
  for (auto& [pid, info] : open_process_details_windows) {
    if (!perception::DoesProcessExist(pid)) {
      pids_to_close.push_back(pid);
    } else {
      // Update UI of open process details window
      size_t memory_used = 0;
      size_t creation_timestamp = 0;
      size_t registered_services = 0;
      uint8 cpu_percentages[8] = {0};
      perception::GetProcessHealthMetrics(pid, memory_used, creation_timestamp,
                                          registered_services, cpu_percentages);

      // Uptime label
      auto now = perception::GetTimeSinceKernelStarted();
      size_t uptime_micros = now.count() - info.creation_timestamp;
      if (info.uptime_label) {
        info.uptime_label->SetText("Uptime: " + FormatUptime(uptime_micros));
      }

      // Memory label
      if (info.memory_label) {
        info.memory_label->SetText(
            "Memory: " + std::to_string(memory_used / 1024) + " KB (" +
            std::to_string(memory_used / 1024 / 1024) + " MB)");
      }

      // CPU labels
      for (int i = 0; i < 8; i++) {
        if (info.cpu_labels[i]) {
          info.cpu_labels[i]->SetText(
              "Core " + std::to_string(i) + ": " +
              std::to_string(((int)cpu_percentages[i] * 100) / 255) + "%");
        }
      }

      // Services table
      if (info.services_data_source && info.services_table) {
        info.services_data_source->UpdateServices();
        info.services_table->Refresh();
      }
    }
  }

  // Close windows for disappeared processes
  for (ProcessId pid : pids_to_close) {
    auto itr = open_process_details_windows.find(pid);
    if (itr != open_process_details_windows.end()) {
      auto window_node = itr->second.window_node;
      open_process_details_windows.erase(itr);
      DecrementCpuTracking();
      window_node.reset();
    }
  }
}
