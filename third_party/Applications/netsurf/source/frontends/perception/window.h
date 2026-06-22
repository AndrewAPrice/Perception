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
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "perception/ui/components/scroll_bar.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"

extern "C" {
#include <stddef.h>
#include <stdint.h>
#include "utils/errors.h"
#include "netsurf/mouse.h"
#include "netsurf/window.h"
}

struct browser_window;

namespace netsurf {
namespace perception {

std::recursive_mutex& GetNetSurfMutex();

class Window {
 public:
  Window();
  ~Window();

  struct browser_window*& GetBrowserWindow() { return bw_; }
  struct browser_window* GetBrowserWindow() const { return bw_; }

  std::shared_ptr<::perception::ui::Node>& GetTabRootNode() {
    return tab_root_node_;
  }
  const std::shared_ptr<::perception::ui::Node>& GetTabRootNode() const {
    return tab_root_node_;
  }

  std::shared_ptr<::perception::ui::Node>& GetContentNode() {
    return content_node_;
  }
  const std::shared_ptr<::perception::ui::Node>& GetContentNode() const {
    return content_node_;
  }

  std::shared_ptr<::perception::ui::components::ScrollBar>&
  GetHorizontalScrollBar() {
    return horizontal_scroll_bar_;
  }
  const std::shared_ptr<::perception::ui::components::ScrollBar>&
  GetHorizontalScrollBar() const {
    return horizontal_scroll_bar_;
  }

  std::shared_ptr<::perception::ui::components::ScrollBar>&
  GetVerticalScrollBar() {
    return vertical_scroll_bar_;
  }
  const std::shared_ptr<::perception::ui::components::ScrollBar>&
  GetVerticalScrollBar() const {
    return vertical_scroll_bar_;
  }

  std::string& GetTitle() { return title_; }
  const std::string& GetTitle() const { return title_; }

  ::perception::ui::Point& GetScroll() { return scroll_; }
  const ::perception::ui::Point& GetScroll() const { return scroll_; }

  std::shared_ptr<bool>& GetIsAlive() { return is_alive_; }
  const std::shared_ptr<bool>& GetIsAlive() const { return is_alive_; }

  bool& GetHasPendingHover() { return has_pending_hover_; }
  bool GetHasPendingHover() const { return has_pending_hover_; }

  ::perception::ui::Point& GetPendingHover() { return pending_hover_; }
  const ::perception::ui::Point& GetPendingHover() const { return pending_hover_; }

  browser_mouse_state& GetPendingHoverState() { return pending_hover_state_; }
  browser_mouse_state GetPendingHoverState() const {
    return pending_hover_state_;
  }

  bool& GetHoverDeferred() { return hover_deferred_; }
  bool GetHoverDeferred() const { return hover_deferred_; }

  ::perception::ui::Point& GetPendingScroll() { return pending_scroll_; }
  const ::perception::ui::Point& GetPendingScroll() const {
    return pending_scroll_;
  }

  bool& GetHasPendingScroll() { return has_pending_scroll_; }
  bool GetHasPendingScroll() const { return has_pending_scroll_; }

  bool& GetScrollDeferred() { return scroll_deferred_; }
  bool GetScrollDeferred() const { return scroll_deferred_; }

  bool& GetExtentDeferred() { return extent_deferred_; }
  bool GetExtentDeferred() const { return extent_deferred_; }

  bool& GetTitleDeferred() { return title_deferred_; }
  bool GetTitleDeferred() const { return title_deferred_; }

 private:
  struct browser_window* bw_ = nullptr;
  std::shared_ptr<::perception::ui::Node> tab_root_node_;
  std::shared_ptr<::perception::ui::Node> content_node_;
  std::shared_ptr<::perception::ui::components::ScrollBar>
      horizontal_scroll_bar_;
  std::shared_ptr<::perception::ui::components::ScrollBar> vertical_scroll_bar_;
  std::string title_;
  ::perception::ui::Point scroll_{.x = 0.0f, .y = 0.0f};

  std::shared_ptr<bool> is_alive_;
  bool has_pending_hover_ = false;
  ::perception::ui::Point pending_hover_{.x = 0.0f, .y = 0.0f};
  browser_mouse_state pending_hover_state_ = BROWSER_MOUSE_HOVER;
  bool hover_deferred_ = false;

  ::perception::ui::Point pending_scroll_{.x = 0.0f, .y = 0.0f};
  bool has_pending_scroll_ = false;
  bool scroll_deferred_ = false;
  bool extent_deferred_ = false;
  bool title_deferred_ = false;
};

extern struct gui_window_table perception_window_table;

}  // namespace perception
}  // namespace netsurf

struct gui_window : public ::netsurf::perception::Window {
  using Window::Window;
};

#define NETSURF_LOCK \
  std::scoped_lock lock(::netsurf::perception::GetNetSurfMutex())
