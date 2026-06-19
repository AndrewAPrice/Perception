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
#include "perception/fibers.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/tab_bar.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/text_alignment.h"
#include "processes_tab.h"
#include "tabs.h"

using ::perception::Defer;
using ::perception::GetService;
using ::perception::devices::GraphicsDevice;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Label;
using ::perception::ui::components::TabBar;
using ::perception::ui::components::UiWindow;

namespace {

std::shared_ptr<Node> launcher_window;
std::shared_ptr<Node> tab_content_container;
std::shared_ptr<TabBar> launcher_tab_bar;

// The current tab that is showing.
std::optional<Tab> current_tab;

enum class WindowState { CLOSED, OPENING, OPEN };
WindowState launcher_window_state = WindowState::CLOSED;
std::vector<::perception::Fiber*> waiting_window_fibers;

}  // namespace

void SwitchToTab(Tab tab) {
  std::cout << "SwitchToTab called. tab = " << (int)tab << std::endl;
  if (current_tab && *current_tab == tab) {
    std::cout << "Already on this tab." << std::endl;
    return;
  }

  if (current_tab && *current_tab == Tab::PROCESSES) {
    SetProcessesTabVisible(false);
  }

  current_tab = tab;
  tab_content_container->RemoveChildren();
  auto tab_contents = GetOrConstructTabContents(tab);
  tab_content_container->AddChild(tab_contents);

  if (launcher_tab_bar) {
    int expected_index = (tab == Tab::APPLICATIONS ? 0 : 1);
    if (launcher_tab_bar->GetSelectedTab() != expected_index)
      launcher_tab_bar->SelectTab(expected_index);
  }

  if (tab == Tab::PROCESSES) SetProcessesTabVisible(true);
}

void ShowLauncherWindow() {
  if (launcher_window_state == WindowState::OPEN) {
    // Launcher window is already open.
    if (launcher_window) launcher_window->Get<UiWindow>()->Focus();
    return;
  }

  if (launcher_window_state == WindowState::OPENING) {
    waiting_window_fibers.push_back(::perception::GetCurrentlyExecutingFiber());
    ::perception::Sleep();
    if (launcher_window) launcher_window->Get<UiWindow>()->Focus();
    return;
  }

  launcher_window_state = WindowState::OPENING;

  // Query the screen size.
  auto screen_size = GetService<GraphicsDevice>().GetScreenSize();
  if (!screen_size.Ok()) {
    launcher_window_state = WindowState::CLOSED;
    for (auto fiber : waiting_window_fibers) fiber->WakeUp();
    waiting_window_fibers.clear();
    return;
  }

  // Create the launcher that's 80% of the screen size.
  int launcher_width = screen_size->width * 8 / 10;
  int launcher_height = screen_size->height * 8 / 10;

  launcher_window = UiWindow::ResizableWindowWithTabBar(
      &launcher_tab_bar,
      [](TabBar& tab_bar) {
        launcher_tab_bar->SetPrefixNode(Label::BasicLabel(
            "Perception", [](Label& lbl) { lbl.SetColor(0xFFFFFFFF); },
            [](Layout& layout) { layout.SetMargin(YGEdgeHorizontal, 8.0f); }));
        launcher_tab_bar->AddTab("Applications");
        launcher_tab_bar->AddTab("Running processes");
        launcher_tab_bar->OnTabSelected([](int index) {
          Tab tab = (index == 0 ? Tab::APPLICATIONS : Tab::PROCESSES);
          if (!current_tab || *current_tab != tab) SwitchToTab(tab);
        });
      },
      [](UiWindow& window) {
        window.SetTitle("Launcher");
        window.OnClose([]() {
          Defer([]() {
            if (current_tab && *current_tab == Tab::PROCESSES) {
              SetProcessesTabVisible(false);
            }
            launcher_window.reset();
            tab_content_container.reset();
            launcher_tab_bar.reset();
            launcher_window_state = WindowState::CLOSED;
          });
        });
      },
      [launcher_width, launcher_height](Layout& layout) {
        layout.SetWidth((float)launcher_width);
        layout.SetHeight((float)launcher_height);
      },
      Node::Empty(&tab_content_container, [](Layout& layout) {
        layout.SetFlexDirection(YGFlexDirectionRow);
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetMinHeight(0.0f);
        layout.SetMargin(YGEdgeAll, -8.0f);
        layout.SetMargin(YGEdgeTop, 0.0f);
      }));

  current_tab = std::nullopt;
  SwitchToTab(Tab::APPLICATIONS);

  launcher_window_state = WindowState::OPEN;
  for (auto fiber : waiting_window_fibers) fiber->WakeUp();
  waiting_window_fibers.clear();
}