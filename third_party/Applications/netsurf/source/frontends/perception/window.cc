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

#include "window.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <string>
#include <vector>

extern "C" {
#include "netsurf/browser_window.h"
#include "netsurf/window.h"
#include "utils/nsurl.h"
}

#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/node.h"

using ::perception::ui::Node;

namespace netsurf {
namespace perception {

std::shared_ptr<::perception::ui::Node> global_ui_window = nullptr;
std::shared_ptr<::perception::ui::components::TabBar> global_tab_bar = nullptr;
std::shared_ptr<::perception::ui::Node> viewport_container = nullptr;
::perception::ui::components::InputBox* global_url_input = nullptr;
::perception::ui::components::Label* global_status_label = nullptr;

std::vector<struct gui_window*> open_tabs;
size_t active_tab_index = 0;

extern void gui_quit();

void UpdateTabBar() {
  if (!global_tab_bar) return;

  global_tab_bar->ClearTabs();
  for (size_t i = 0; i < open_tabs.size(); i++) {
    auto gw = open_tabs[i];
    std::string title = gw->title;
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
  viewport_container->AddChild(active_gw->tab_root_node);

  if (global_url_input && active_gw->bw) {
    struct nsurl* url = nullptr;
    nserror ret = browser_window_get_url(active_gw->bw, true, &url);
    if (ret == NSERROR_OK && url) global_url_input->SetText(nsurl_access(url));
  }

  if (active_gw->bw) {
    browser_window_schedule_reformat(active_gw->bw);
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

  struct gui_window* closed_gw = open_tabs[index];
  browser_window_destroy(closed_gw->bw);
}

}  // namespace perception
}  // namespace netsurf
