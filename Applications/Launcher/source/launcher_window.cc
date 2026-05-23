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

#include "launcher_window.h"

#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include "perception/devices/graphics_device.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/text_alignment.h"
#include "processes_tab.h"
#include "side_panel.h"
#include "tabs.h"

using ::perception::Defer;
using ::perception::GetService;
using ::perception::devices::GraphicsDevice;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Label;
using ::perception::ui::components::UiWindow;

namespace {

std::shared_ptr<Node> launcher_window;
std::shared_ptr<Node> tab_content_container;

// The current tab that is showing.
std::optional<Tab> current_tab;

}  // namespace

void SwitchToTab(Tab tab) {
  if (current_tab && *current_tab == tab) return;  // Already on this tab.

  if (current_tab && *current_tab == Tab::PROCESSES) {
    SetProcessesTabVisible(false);
  }

  current_tab = tab;
  tab_content_container->RemoveChildren();
  tab_content_container->AddChildren(
      {GetOrConstructSidePanel(), GetOrConstructTabContents(tab)});

  if (tab == Tab::PROCESSES) {
    SetProcessesTabVisible(true);
  }
}

void ShowLauncherWindow() {
  if (launcher_window) {
    // Launcher window is already open.
    // TODO: Bring to front.
    launcher_window->Get<UiWindow>()->Focus();
    return;
  }

  // Query the screen size.
  auto screen_size = GetService<GraphicsDevice>().GetScreenSize();
  if (!screen_size.Ok()) return;

  // Create the launcher that's 80% of the screen size.
  int launcher_width = screen_size->width * 8 / 10;
  int launcher_height = screen_size->height * 8 / 10;

  tab_content_container = Node::Empty([](Layout& layout) {
    layout.SetFlexDirection(YGFlexDirectionRow);
    layout.SetFlexGrow(1.0f);
    layout.SetMargin(YGEdgeAll, -8.0f);
  });

  launcher_window = UiWindow::ResizableWindowWithTitleBar(
      "Launcher",
      [](UiWindow& window) {
        window.OnClose([]() {
          Defer([]() {
            if (current_tab && *current_tab == Tab::PROCESSES) {
              SetProcessesTabVisible(false);
            }
            launcher_window.reset();
            tab_content_container.reset();
          });
        });
      },
      [launcher_width, launcher_height](Layout& layout) {
        layout.SetWidth((float)launcher_width);
        layout.SetHeight((float)launcher_height);
      },
      tab_content_container);

  current_tab = std::nullopt;
  SwitchToTab(Tab::APPLICATIONS);
}