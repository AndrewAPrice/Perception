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

#include "side_panel.h"

#include <memory>

#include "launcher_window.h"
#include "perception/ui/button.h"
#include "perception/ui/container.h"
#include "perception/ui/widget.h"
#include "tabs.h"

using ::perception::ui::Button;
using ::perception::ui::Container;
using ::perception::ui::Widget;

namespace {

// Widget containing the side panel.
std::shared_ptr<Widget> side_panel;

}

// Gets or constructs the side panel of the launcher.
std::shared_ptr<Widget> GetOrConstructSidePanel() {
  if (side_panel) return side_panel;

  side_panel = Container::Create()
                   ->SetBorderRadius(0.0f)
                   ->SetBorderWidth(0.0f)
                   ->SetBackgroundColor(0x2518E9FF)
                   ->SetFlexDirection(YGFlexDirectionColumn)
                   ->SetWidthAuto()
                   ->SetHeightPercent(100.0f)
                   ->ToSharedPtr();

  for (Tab tab : GetSidePanelTabsToShow()) {
    side_panel->AddChild(Button::Create()
                             ->SetLabel(GetTabLabel(tab))
                             ->OnClick([tab]() { SwitchToTab(tab); })
                             ->ToSharedPtr());
  }
  return side_panel;
}
