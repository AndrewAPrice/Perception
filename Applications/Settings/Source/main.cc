// Copyright 2021 Google LLC
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

#include <iostream>
#include <map>
#include <string>

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

using ::perception::ForEachProcess;
using ::perception::ForEachService;
using ::perception::GetProcessName;
using ::perception::GetServiceName;
using ::perception::HandOverControl;
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
using ::perception::ui::Widget;

namespace {

double kPidLabelWidth = 25.0f;
double kTerminateProcessButtonWidth = 1.0f;

}  // namespace

void RebuildRunningApplications(
    std::shared_ptr<Widget> running_applications_container);

std::shared_ptr<Widget> CreateApplication(
    size_t pid, std::string_view process_name,
    const std::vector<std::string> service_names,
    std::shared_ptr<Widget> running_applications_container) {
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
                             ->OnClick([pid, running_applications_container] {
                               TerminateProcesss(pid);
                               RebuildRunningApplications(
                                   running_applications_container);
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

void RebuildRunningApplications(
    std::shared_ptr<Widget> running_applications_container) {
  running_applications_container->RemoveChildren();

  std::vector<ProcessId> pids;
  ForEachProcess([&](ProcessId process_id) {
    pids.push_back(process_id);
  });

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
    process_widgets.push_back(CreateApplication(
        process_id, name,
        services_by_pids.contains(process_id) ? services_by_pids[process_id]
                                              : std::vector<std::string>(),
        running_applications_container));
  }
  running_applications_container->AddChildren(process_widgets);
}

int main() {
  auto running_applications_container = std::make_shared<Widget>();
  RebuildRunningApplications(running_applications_container);

  auto window = std::make_shared<UiWindow>("Settings");
  window->AddChildren(
      {Button::Create()
           ->SetLabel("Refresh")
           ->OnClick([running_applications_container]() {
             RebuildRunningApplications(running_applications_container);
           })
           ->ToSharedPtr(),
       ScrollContainer::Create(
           ParentlessWidget::Create(running_applications_container),
           /*show_vertical_scroll_bar=*/true,
           /*show_horizontal_scroll_bar=*/false)
           ->SetFlexGrow(1.0)
           ->SetWidthPercent(100)
           ->ToSharedPtr()});
  window->Create();
  HandOverControl();

  return 0;
}
