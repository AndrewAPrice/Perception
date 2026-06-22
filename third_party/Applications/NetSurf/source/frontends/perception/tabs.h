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

#pragma once

#include <memory>
#include <vector>

namespace perception {
namespace ui {
class Node;
namespace components {
class TabBar;
class InputBox;
class Label;
}  // namespace components
}  // namespace ui
}  // namespace perception

namespace netsurf {
namespace perception {

class Window;

// Tab management operations
void UpdateTabBar();
void SwitchTab(size_t index);
void CloseTab(size_t index);
void HandleTabDestroyed(Window* gw);
void UpdateScrollBars(Window* gw);

// Tab collection accessors
void AddTab(Window* gw);
void RemoveTab(Window* gw);
size_t GetTabCount();
Window* GetTab(size_t index);
Window* GetActiveTab();
size_t GetActiveTabIndex();
void SetActiveTabIndex(size_t index);
const std::vector<Window*>& GetOpenTabs();

// Global UI element accessors
std::shared_ptr<::perception::ui::Node> GetGlobalUiWindow();
void SetGlobalUiWindow(std::shared_ptr<::perception::ui::Node> window);

std::shared_ptr<::perception::ui::components::TabBar> GetGlobalTabBar();
void SetGlobalTabBar(
    std::shared_ptr<::perception::ui::components::TabBar> tab_bar);

std::shared_ptr<::perception::ui::Node> GetViewportContainer();
void SetViewportContainer(
    std::shared_ptr<::perception::ui::Node> container);

::perception::ui::components::InputBox* GetGlobalUrlInput();
void SetGlobalUrlInput(::perception::ui::components::InputBox* input);

::perception::ui::components::Label* GetGlobalStatusLabel();
void SetGlobalStatusLabel(::perception::ui::components::Label* label);

}  // namespace perception
}  // namespace netsurf
