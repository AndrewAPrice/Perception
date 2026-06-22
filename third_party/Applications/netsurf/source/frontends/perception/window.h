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
#include <string>
#include <vector>
#include <mutex>

#include "perception/ui/components/tab_bar.h"
#include "perception/ui/point.h"

extern "C" {
#include "netsurf/mouse.h"
}

namespace perception {
namespace ui {
namespace components {
class InputBox;
class Label;
class ScrollBar;
}
}
}

struct gui_window {
	gui_window() : is_alive(std::make_shared<bool>(true)), has_pending_hover(false), pending_hover_state(BROWSER_MOUSE_HOVER), hover_deferred(false), scroll({.x = 0.0f, .y = 0.0f}), pending_hover({.x = 0.0f, .y = 0.0f}), pending_scroll({.x = 0.0f, .y = 0.0f}), has_pending_scroll(false), scroll_deferred(false) {}
	~gui_window() {
		*is_alive = false;
	}

	struct browser_window *bw;
	std::shared_ptr<::perception::ui::Node> tab_root_node;
	std::shared_ptr<::perception::ui::Node> content_node;
	std::shared_ptr<::perception::ui::components::ScrollBar> horizontal_scroll_bar;
	std::shared_ptr<::perception::ui::components::ScrollBar> vertical_scroll_bar;
	std::string title;
	::perception::ui::Point scroll;

	std::shared_ptr<bool> is_alive;
	bool has_pending_hover;
	::perception::ui::Point pending_hover;
	browser_mouse_state pending_hover_state;
	bool hover_deferred;

	::perception::ui::Point pending_scroll;
	bool has_pending_scroll;
	bool scroll_deferred;
	bool extent_deferred = false;
	bool title_deferred = false;
};

namespace netsurf {
namespace perception {

extern std::recursive_mutex netsurf_mutex;

extern std::shared_ptr<::perception::ui::Node> global_ui_window;
extern std::shared_ptr<::perception::ui::components::TabBar> global_tab_bar;
extern std::shared_ptr<::perception::ui::Node> viewport_container;
extern ::perception::ui::components::InputBox *global_url_input;
extern ::perception::ui::components::Label *global_status_label;

extern std::vector<struct gui_window*> open_tabs;
extern size_t active_tab_index;

void UpdateTabBar();
void SwitchTab(size_t index);
void CloseTab(size_t index);
void UpdateScrollBars(struct gui_window* gw);


}  // namespace perception
}  // namespace netsurf

#define NETSURF_LOCK std::scoped_lock lock(::netsurf::perception::netsurf_mutex)
