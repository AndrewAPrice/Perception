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
#include <vector>

#include "launcher_window.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "tabs.h"

using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;

namespace {

// Widget containing the side panel.
std::shared_ptr<Node> side_panel;

}  // namespace

// Gets or constructs the side panel of the launcher.
std::shared_ptr<Node> GetOrConstructSidePanel() {
  if (side_panel) return side_panel;

  side_panel = Container::VerticalContainer(
      [](Block& block) {
        block.SetBorderRadius(0.0f);
        block.SetBorderWidth(0.0f);
        block.SetFillColor(0x2518E9FF);
      },
      [](Layout& layout) {
        layout.SetWidthAuto();
        layout.SetHeightPercent(100.0f);
        layout.SetPadding(YGEdgeAll, 8.0f);
      });

  for (Tab tab : GetSidePanelTabsToShow()) {
    side_panel->AddChild(
        Button::TextButton(GetTabLabel(tab), [tab]() { SwitchToTab(tab); }));
  }
  return side_panel;
}
