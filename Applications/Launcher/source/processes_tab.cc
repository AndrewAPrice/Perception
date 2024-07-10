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
#if 0
#include "processes_tab.h"

#include <iostream>
#include <map>
#include <memory>
#include <string>

#include "launcher_window.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/ui/button.h"
#include "perception/ui/container.h"
#include "perception/ui/parentless_widget.h"
#include "perception/ui/scroll_bar.h"
#include "perception/ui/scroll_container.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/ui_window.h"
#include "perception/ui/node.h"
#include "tabs.h"

using ::perception::ForEachProcess;
using ::perception::ForEachService;
using ::perception::GetProcessName;
using ::perception::GetServiceName;
using ::perception::MessageId;
using ::perception::ProcessId;
using ::perception::TerminateProcesss;
using ::perception::ui::Button;
using ::perception::ui::Container;
using ::perception::ui::Label;
using ::perception::ui::ParentlessWidget;
using ::perception::ui::ScrollBar;
using ::perception::ui::ScrollContainer;
using ::perception::ui::TextAlignment;
using ::perception::ui::UiWindow;
using ::perception::ui::Node;

namespace {

// The size of the pid label.
double kPidLabelWidth = 25.0f;

// The width of the terminate button.
double kTerminateProcessButtonWidth = 1.0f;

// Widget containing the processes tab.
std::shared_ptr<Widget> processes_tab;

// Container of the running processes.
std::shared_ptr<Widget> running_processes_container;

// Rebuilds the containing of running processes.
void RebuildRunningProcesses();

// Builds a widget representing a process.
std::shared_ptr<Widget> CreateProcess(
    size_t pid, std::string_view process_name,
    const std::vector<std::string> service_names) {
  auto process_row =
      std::make_shared<Widget>()
          ->SetWidthPercent(100)
          ->SetFlexDirection(YGFlexDirectionRow)
          ->AddChildren({std::make_shared<Label>()
                             ->SetLabel(std::to_string(pid))
                             ->SetTextAlignment(TextAlignment::MiddleLeft)
                             ->SetWidth(kPidLabelWidth)
                             ->ToSharedPtr(),
                         std::make_shared<Label>()
                             ->SetLabel(process_name)
                             ->SetTextAlignment(TextAlignment::MiddleLeft)
                             ->SetFlexGrow(1.0f)
                             ->ToSharedPtr(),
                         Button::Create()
                             ->SetLabel("x")
                             ->OnClick([pid] {
                               TerminateProcesss(pid);
                               RebuildRunningProcesses();
                             })
                             ->SetWidth(kTerminateProcessButtonWidth)
                             ->ToSharedPtr()})
          ->ToSharedPtr();

  if (service_names.empty()) return process_row;

  auto parent = std::make_shared<Widget>()
                    ->SetWidthPercent(100)
                    ->SetFlexDirection(YGFlexDirectionColumn)
                    ->AddChild(process_row)
                    ->ToSharedPtr();

  for (const auto& service_name : service_names) {
    parent->AddChild(
        std::make_shared<Label>()->SetLabel(service_name)->ToSharedPtr());
  }

  return parent;
}

// Rebuilds the container holding all of the running processes.
void RebuildRunningProcesses() {
  if (!running_processes_container)
    running_processes_container = std::make_shared<Widget>();
  running_processes_container->RemoveChildren();

  std::vector<ProcessId> pids;
  ForEachProcess([&](ProcessId process_id) { pids.push_back(process_id); });

  std::map<ProcessId, std::vector<std::string>> services_by_pids;
  ForEachService([&](ProcessId process_id, MessageId message_id) {
    std::string service_name = GetServiceName(process_id, message_id);
    if (service_name.empty()) return;

    if (!services_by_pids.contains(process_id))
      services_by_pids[process_id] = std::vector<std::string>();
    services_by_pids[process_id].push_back(service_name);
  });

  std::vector<std::shared_ptr<Widget>> process_widgets;

  for (ProcessId process_id : pids) {
    std::string name = GetProcessName(process_id);
    if (name.empty()) continue;
    process_widgets.push_back(CreateProcess(
        process_id, name,
        services_by_pids.contains(process_id) ? services_by_pids[process_id]
                                              : std::vector<std::string>()));
  }
  running_processes_container->AddChildren(process_widgets);
}

}  // namespace

// Gets or constructs the processes tab of the launcher.
std::shared_ptr<Widget> GetOrConstructProcessesTab() {
  RebuildRunningProcesses();

  if (processes_tab) return processes_tab;

  processes_tab =
      Container::Create()
          ->SetBorderRadius(0.0f)
          ->SetBorderWidth(0.0f)
          ->SetBackgroundColor(0x2518E9FF)
          ->SetFlexDirection(YGFlexDirectionColumn)
          ->SetFlexGrow(1.0)
          ->SetHeightPercent(100.0f)
          ->AddChildren(
              {Button::Create()
                   ->SetLabel("Refresh")
                   ->OnClick([]() { RebuildRunningProcesses(); })
                   ->ToSharedPtr(),
               ScrollContainer::Create(
                   ParentlessWidget::Create(running_processes_container),
                   /*show_vertical_scroll_bar=*/true,
                   /*show_horizontal_scroll_bar=*/false)
                   ->SetFlexGrow(1.0)
                   ->SetWidthPercent(100.0f)
                   ->ToSharedPtr()})
          ->ToSharedPtr();

  return processes_tab;
}

#endif