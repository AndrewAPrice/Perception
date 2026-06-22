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

#include "tabs.h"

#include <algorithm>
#include <string>

extern "C" {
#include "netsurf/browser_window.h"
#include "netsurf/window.h"
#include "utils/nsurl.h"
}

#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_bar.h"
#include "perception/ui/components/tab_bar.h"
#include "perception/ui/node.h"
#include "window.h"

namespace netsurf {
namespace perception {

namespace {

std::shared_ptr<::perception::ui::Node> global_ui_window = nullptr;
std::shared_ptr<::perception::ui::components::TabBar> global_tab_bar = nullptr;
std::shared_ptr<::perception::ui::Node> viewport_container = nullptr;
::perception::ui::components::InputBox* global_url_input = nullptr;
::perception::ui::components::Label* global_status_label = nullptr;

std::vector<Window*> open_tabs;
size_t active_tab_index = 0;

}  // namespace

void gui_quit();

void UpdateTabBar() {
  if (!global_tab_bar) return;

  global_tab_bar->ClearTabs();
  for (size_t i = 0; i < open_tabs.size(); i++) {
    auto gw = open_tabs[i];
    std::string title = gw->GetTitle();
    if (title.empty()) title = "New Tab";
    global_tab_bar->AddTab(title);
  }
  global_tab_bar->SelectTab((int)active_tab_index);
}

void SwitchTab(size_t index) {
  NETSURF_LOCK;
  if (index >= open_tabs.size()) return;

  active_tab_index = index;
  auto active_gw = open_tabs[index];

  viewport_container->RemoveChildren();
  viewport_container->AddChild(active_gw->GetTabRootNode());

  if (global_url_input && active_gw->GetBrowserWindow()) {
    struct nsurl* url = nullptr;
    nserror ret =
        browser_window_get_url(active_gw->GetBrowserWindow(), true, &url);
    if (ret == NSERROR_OK && url) {
      global_url_input->SetText(nsurl_access(url));
    }
  }

  if (active_gw->GetBrowserWindow()) {
    browser_window_schedule_reformat(active_gw->GetBrowserWindow());
    UpdateScrollBars(active_gw);
  }

  UpdateTabBar();
  if (global_ui_window) global_ui_window->Invalidate();
}

void CloseTab(size_t index) {
  NETSURF_LOCK;
  if (index >= open_tabs.size()) return;
  if (open_tabs.size() <= 1) {
    gui_quit();
    return;
  }

  Window* closed_gw = open_tabs[index];
  if (closed_gw->GetBrowserWindow()) {
    browser_window_destroy(closed_gw->GetBrowserWindow());
  }
}

void HandleTabDestroyed(Window* gw) {
  auto it = std::find(open_tabs.begin(), open_tabs.end(), gw);
  if (it == open_tabs.end()) return;

  size_t index = std::distance(open_tabs.begin(), it);
  open_tabs.erase(it);

  if (open_tabs.empty()) {
    gui_quit();
    return;
  }

  if (active_tab_index >= open_tabs.size()) {
    active_tab_index = open_tabs.size() - 1;
  } else if (active_tab_index == index) {
    active_tab_index = (index > 0) ? index - 1 : 0;
  }

  SwitchTab(active_tab_index);
}

void UpdateScrollBars(Window* gw) {
  if (!gw || !gw->GetContentNode()) return;

  int content_width = 0;
  int content_height = 0;
  if (gw->GetBrowserWindow()) {
    browser_window_get_extents(gw->GetBrowserWindow(), false, &content_width,
                               &content_height);
  }

  if (content_width == 0) content_width = 800;
  if (content_height == 0) content_height = 600;

  float viewport_width = gw->GetContentNode()->GetSize().width;
  float viewport_height = gw->GetContentNode()->GetSize().height;

  if (viewport_width < 1.0f) viewport_width = 1.0f;
  if (viewport_height < 1.0f) viewport_height = 1.0f;

  if (gw->GetHorizontalScrollBar()) {
    gw->GetHorizontalScrollBar()->SetValue(
        0.0f, (float)content_width, (float)gw->GetScroll().x, viewport_width);
  }
  if (gw->GetVerticalScrollBar()) {
    gw->GetVerticalScrollBar()->SetValue(
        0.0f, (float)content_height, (float)gw->GetScroll().y,
        viewport_height);
  }
}

void AddTab(Window* gw) {
  open_tabs.push_back(gw);
}

void RemoveTab(Window* gw) {
  auto it = std::find(open_tabs.begin(), open_tabs.end(), gw);
  if (it != open_tabs.end()) {
    open_tabs.erase(it);
  }
}

size_t GetTabCount() {
  return open_tabs.size();
}

Window* GetTab(size_t index) {
  if (index < open_tabs.size()) {
    return open_tabs[index];
  }
  return nullptr;
}

Window* GetActiveTab() {
  if (active_tab_index < open_tabs.size()) {
    return open_tabs[active_tab_index];
  }
  return nullptr;
}

size_t GetActiveTabIndex() {
  return active_tab_index;
}

void SetActiveTabIndex(size_t index) {
  active_tab_index = index;
}

const std::vector<Window*>& GetOpenTabs() {
  return open_tabs;
}

std::shared_ptr<::perception::ui::Node> GetGlobalUiWindow() {
  return global_ui_window;
}

void SetGlobalUiWindow(std::shared_ptr<::perception::ui::Node> window) {
  global_ui_window = window;
}

std::shared_ptr<::perception::ui::components::TabBar> GetGlobalTabBar() {
  return global_tab_bar;
}

void SetGlobalTabBar(
    std::shared_ptr<::perception::ui::components::TabBar> tab_bar) {
  global_tab_bar = tab_bar;
}

std::shared_ptr<::perception::ui::Node> GetViewportContainer() {
  return viewport_container;
}

void SetViewportContainer(std::shared_ptr<::perception::ui::Node> container) {
  viewport_container = container;
}

::perception::ui::components::InputBox* GetGlobalUrlInput() {
  return global_url_input;
}

void SetGlobalUrlInput(::perception::ui::components::InputBox* input) {
  global_url_input = input;
}

::perception::ui::components::Label* GetGlobalStatusLabel() {
  return global_status_label;
}

void SetGlobalStatusLabel(::perception::ui::components::Label* label) {
  global_status_label = label;
}

}  // namespace perception
}  // namespace netsurf
