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

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

extern "C" {
#include "utils/errors.h"
#include "netsurf/core_window.h"
#include "netsurf/mouse.h"
#include "netsurf/plotters.h"
#include "netsurf/types.h"
}

#include "perception/ui/components/scroll_bar.h"
#include "perception/ui/node.h"
#include "perception/window/keyboard_key_event.h"
#include "perception/window/mouse_button.h"

namespace netsurf {
namespace perception {

class CoreWindowHost;

}  // namespace perception
}  // namespace netsurf

struct core_window {
  ::netsurf::perception::CoreWindowHost* host = nullptr;
};

namespace netsurf {
namespace perception {

extern struct core_window_table perception_core_window_table;

// Hosts a NetSurf core_window inside a Perception UI Node with automatic scrollbars.
class CoreWindowHost : public std::enable_shared_from_this<CoreWindowHost> {
 public:
  using DrawCallback = std::function<void(const struct rect& clip,
                                          const struct redraw_context& ctx)>;
  using MouseCallback =
      std::function<void(browser_mouse_state state, int x, int y)>;
  using KeyCallback = std::function<bool(uint32_t nskey)>;

  // Creates and initializes a new CoreWindowHost with the specified callbacks.
  static std::shared_ptr<CoreWindowHost> Create(DrawCallback on_draw,
                                                MouseCallback on_mouse,
                                                KeyCallback on_key);

  ~CoreWindowHost();

  // Returns the underlying C core_window handle.
  struct core_window* GetCoreWindow();

  // Returns the root container node including scrollbars.
  std::shared_ptr<::perception::ui::Node> GetRootNode() const;

  // Returns the inner canvas node where content is drawn.
  std::shared_ptr<::perception::ui::Node> GetCanvasNode() const;

  // Returns the logical content width in pixels.
  int GetContentWidth() const;

  // Returns the logical content height in pixels.
  int GetContentHeight() const;

  // Invalidates the specified area (or the entire canvas if r is null).
  void InvalidateArea(const struct rect* r);

  // Updates the logical extent of the core window content.
  void SetExtent(int width, int height);

  // Scrolls the viewport to the given coordinates.
  void SetScroll(int x, int y);

  // Retrieves the current scroll offsets.
  void GetScroll(int* x, int* y) const;

  // Retrieves the current viewport dimensions.
  void GetDimensions(int* width, int* height) const;

  // Updates the current drag status of the core window.
  void SetDragStatus(core_window_drag_status status);

 private:
  CoreWindowHost(DrawCallback on_draw, MouseCallback on_mouse,
                 KeyCallback on_key);

  void BuildUi();
  void UpdateScrollBars();
  void ScheduleScrollBarUpdate();
  browser_mouse_state GetModifierFlags() const;
  void HandleKeyDown(const ::perception::window::KeyboardKeyEvent& event);
  void HandleKeyUp(const ::perception::window::KeyboardKeyEvent& event);

  struct core_window core_window_wrapper_;
  DrawCallback on_draw_;
  MouseCallback on_mouse_;
  KeyCallback on_key_;

  std::shared_ptr<::perception::ui::Node> root_node_;
  std::shared_ptr<::perception::ui::Node> canvas_node_;
  std::shared_ptr<::perception::ui::Node> h_scroll_bar_node_;
  std::shared_ptr<::perception::ui::Node> v_scroll_bar_node_;
  std::shared_ptr<::perception::ui::components::ScrollBar> h_scroll_bar_;
  std::shared_ptr<::perception::ui::components::ScrollBar> v_scroll_bar_;

  int content_width_ = 0;
  int content_height_ = 0;
  int scroll_x_ = 0;
  int scroll_y_ = 0;
  int last_viewport_width_ = 0;
  int last_viewport_height_ = 0;
  core_window_drag_status drag_status_ = CORE_WINDOW_DRAG_NONE;

  bool is_mouse_down_ = false;
  bool drag_started_ = false;
  int press_x_ = 0;
  int press_y_ = 0;
  ::perception::window::MouseButton mouse_button_down_ =
      ::perception::window::MouseButton::Left;

  std::chrono::steady_clock::time_point last_click_time_{};
  int last_click_x_ = 0;
  int last_click_y_ = 0;

  bool shift_pressed_ = false;
  bool ctrl_pressed_ = false;
  bool scrollbar_update_scheduled_ = false;
  std::shared_ptr<bool> is_alive_;
};

}  // namespace perception
}  // namespace netsurf
