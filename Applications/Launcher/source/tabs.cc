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
#include "tabs.h"

#include <string_view>
#include <vector>

#include "applications_tab.h"
#include "perception/ui/label.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/node.h"
#include "processes_tab.h"

using ::perception::ui::Label;
using ::perception::ui::TextAlignment;
using ::perception::ui::Node;

std::string_view GetTabLabel(Tab tab) {
  switch (tab) {
    case Tab::APPLICATIONS:
      return "Applications";
    case Tab::PROCESSES:
      return "Processes";
    case Tab::SETTINGS:
      return "Settings";
  }
}

std::vector<Tab> GetSidePanelTabsToShow() {
  return {Tab::APPLICATIONS, Tab::PROCESSES, Tab::SETTINGS};
}

std::shared_ptr<Widget> GetOrConstructTabContents(Tab tab) {
  switch (tab) {
    case Tab::APPLICATIONS:
      return GetOrConstructApplicationsTab();
    case Tab::PROCESSES:
    return GetOrConstructProcessesTab();
    case Tab::SETTINGS:
      return std::make_shared<Label>()
          ->SetTextAlignment(TextAlignment::MiddleCenter)
          ->SetLabel("TODO: Settings")
          ->SetHeightPercent(100.0f)
          ->SetFlexGrow(1.0)
          ->ToSharedPtr();
  }
}
#endif