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

#if 0
#include "perception/devices/graphics_device.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/ui/label.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/ui_window.h"
#include "side_panel.h"
using ::perception::Defer;
using ::perception::TerminateProcess;
using ::perception::ui::kFillParent;
using ::perception::GetService;
using ::perception::ui::Label;
using ::perception::ui::TextAlignment;
using ::perception::ui::UiWindow;
using ::perception::devices::GraphicsDevice;

namespace {

std::shared_ptr<UiWindow> launcher_window;

// The current tab that is showing.
std::optional<Tab> current_tab;

}  // namespace

void SwitchToTab(Tab tab) {
  if (current_tab && *current_tab == tab) return;  // Already on this tab.
  current_tab = tab;
  launcher_window->RemoveChildren()->AddChildren(
      {GetOrConstructSidePanel(), GetOrConstructTabContents(tab)});
}

void ShowLauncherWindow() {
  if (launcher_window) {
    // Launcher window is already open.
    return;
  }

  // Query the screen size.
  auto screen_size = GetService<GraphicsDevice>().GetScreenSize();

  // Create the launcher that's 80% of the screen size.
  int launcher_width = screen_size.GetWidth() * 8 / 10;
  int launcher_height = screen_size.GetHeight() * 8 / 10;

  launcher_window = std::make_shared<UiWindow>("Launcher", true);
  launcher_window->OnClose([]() { Defer([]() { launcher_window.reset(); }); })
      ->SetWidth(launcher_width)
      ->SetHeight(launcher_height)
      ->SetFlexDirection(YGFlexDirectionRow)
      ->SetPadding(YGEdgeAll, 0.0f);

  current_tab = std::nullopt;
  SwitchToTab(Tab::APPLICATIONS);

  launcher_window->Create();
}
#endif