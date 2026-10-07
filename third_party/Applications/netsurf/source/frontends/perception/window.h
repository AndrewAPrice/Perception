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
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perception/fibers.h"
#include "perception/ui/components/scroll_bar.h"
#include "perception/ui/image.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/window/cursor.h"
#include "perception/window/keyboard_key_event.h"
#include "perception/window/mouse_button.h"

extern "C" {
#include <stddef.h>
#include <stdint.h>
#include "utils/errors.h"
#include "netsurf/mouse.h"
#include "netsurf/search.h"
#include "netsurf/types.h"
#include "netsurf/window.h"
}

struct browser_window;

namespace netsurf {
namespace perception {

class FiberRecursiveMutex {
 public:
  void lock() {
    ::perception::Fiber* current = ::perception::GetCurrentlyExecutingFiber();
    if (owner_ == current) {
      count_++;
      return;
    }
    while (owner_ != nullptr) {
      waiters_.push_back(current);
      ::perception::Sleep();
    }
    owner_ = current;
    count_ = 1;
  }

  void unlock() {
    ::perception::Fiber* current = ::perception::GetCurrentlyExecutingFiber();
    if (owner_ != current)
      return;
    count_--;
    if (count_ == 0) {
      owner_ = nullptr;
      if (!waiters_.empty()) {
        ::perception::Fiber* next = waiters_.front();
        waiters_.pop_front();
        next->WakeUp();
      }
    }
  }

 private:
  ::perception::Fiber* owner_ = nullptr;
  size_t count_ = 0;
  std::deque<::perception::Fiber*> waiters_;
};

FiberRecursiveMutex& GetNetSurfMutex();

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

  std::shared_ptr<::perception::ui::Node>& GetHorizontalScrollBarNode() {
    return horizontal_scroll_bar_node_;
  }
  const std::shared_ptr<::perception::ui::Node>& GetHorizontalScrollBarNode()
      const {
    return horizontal_scroll_bar_node_;
  }

  std::shared_ptr<::perception::ui::Node>& GetVerticalScrollBarNode() {
    return vertical_scroll_bar_node_;
  }
  const std::shared_ptr<::perception::ui::Node>& GetVerticalScrollBarNode()
      const {
    return vertical_scroll_bar_node_;
  }

  std::string& GetTitle() { return title_; }
  const std::string& GetTitle() const { return title_; }

  const std::shared_ptr<::perception::ui::Image>& GetFavicon() const {
    return favicon_;
  }
  void SetFavicon(std::shared_ptr<::perception::ui::Image> favicon) {
    favicon_ = std::move(favicon);
  }

  bool IsThrobberRunning() const { return throbber_running_; }
  void SetThrobberRunning(bool running) { throbber_running_ = running; }

  ::perception::ui::Point& GetScroll() { return scroll_; }
  const ::perception::ui::Point& GetScroll() const { return scroll_; }

  std::shared_ptr<bool>& GetIsAlive() { return is_alive_; }
  const std::shared_ptr<bool>& GetIsAlive() const { return is_alive_; }

  bool& GetHasPendingHover() { return has_pending_hover_; }
  bool GetHasPendingHover() const { return has_pending_hover_; }

  ::perception::ui::Point& GetPendingHover() { return pending_hover_; }
  const ::perception::ui::Point& GetPendingHover() const {
    return pending_hover_;
  }

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

  int GetLastFormatWidth() const { return last_format_width_; }
  void SetLastFormatWidth(int w) { last_format_width_ = w; }
  int GetLastFormatHeight() const { return last_format_height_; }
  void SetLastFormatHeight(int h) { last_format_height_ = h; }

  // Positions the text caret at the given coordinates with the specified height and optional clipping.
  void PlaceCaret(int x, int y, int height, const struct rect* clip);

  // Removes the text caret.
  void RemoveCaret();

  // Returns true if the text caret is active.
  bool HasCaret() const { return has_caret_; }

  // Returns the x coordinate of the text caret in document coordinates.
  int GetCaretX() const { return caret_x_; }

  // Returns the y coordinate of the text caret in document coordinates.
  int GetCaretY() const { return caret_y_; }

  // Returns the height of the text caret.
  int GetCaretHeight() const { return caret_height_; }

  // Returns true if the caret has a clipping rectangle.
  bool HasCaretClip() const { return has_caret_clip_; }

  // Returns the caret's clipping rectangle.
  const struct rect& GetCaretClip() const { return caret_clip_; }

  // Returns true if the mouse button is pressed down.
  bool IsMouseDown() const { return is_mouse_down_; }

  // Sets whether the mouse button is pressed down.
  void SetMouseDown(bool down) { is_mouse_down_ = down; }

  // Returns true if a drag operation has commenced.
  bool HasDragStarted() const { return drag_started_; }

  // Sets whether a drag operation has commenced.
  void SetDragStarted(bool drag) { drag_started_ = drag; }

  // Returns the document x coordinate where the mouse was pressed down.
  int GetPressDocX() const { return press_doc_x_; }

  // Returns the document y coordinate where the mouse was pressed down.
  int GetPressDocY() const { return press_doc_y_; }

  // Sets the document coordinates where the mouse was pressed down.
  void SetPressDocCoordinates(int x, int y) {
    press_doc_x_ = x;
    press_doc_y_ = y;
  }

  // Returns the mouse button that was pressed down.
  ::perception::window::MouseButton GetMouseButtonDown() const {
    return mouse_button_down_;
  }

  // Sets the mouse button that was pressed down.
  void SetMouseButtonDown(::perception::window::MouseButton button) {
    mouse_button_down_ = button;
  }

  // Sets whether shift key is pressed.
  void SetShiftPressed(bool pressed) { shift_pressed_ = pressed; }

  // Sets whether control key is pressed.
  void SetCtrlPressed(bool pressed) { ctrl_pressed_ = pressed; }

  // Sets whether alt key is pressed.
  void SetAltPressed(bool pressed) { alt_pressed_ = pressed; }

  // Returns the active keyboard modifier flags for NetSurf mouse events.
  browser_mouse_state GetMouseModifiers() const;

  // Records a mouse click and returns any double or triple click flags.
  browser_mouse_state RecordClickAndGetMultiClickFlags(int doc_x, int doc_y);

  // Scrolls the viewport by the given delta in pixels.
  void ScrollViewportBy(float dx, float dy);

  // Scrolls the viewport to the given absolute y coordinate in pixels.
  void ScrollViewportToY(float y);

  // Handles keyboard key down events.
  void HandleKeyDown(const ::perception::window::KeyboardKeyEvent& event);

  // Handles keyboard key up events.
  void HandleKeyUp(const ::perception::window::KeyboardKeyEvent& event);

 private:
  struct browser_window* bw_ = nullptr;
  std::shared_ptr<::perception::ui::Node> tab_root_node_;
  std::shared_ptr<::perception::ui::Node> content_node_;
  std::shared_ptr<::perception::ui::components::ScrollBar>
      horizontal_scroll_bar_;
  std::shared_ptr<::perception::ui::components::ScrollBar> vertical_scroll_bar_;
  std::shared_ptr<::perception::ui::Node> horizontal_scroll_bar_node_;
  std::shared_ptr<::perception::ui::Node> vertical_scroll_bar_node_;
  std::string title_;
  std::shared_ptr<::perception::ui::Image> favicon_;
  bool throbber_running_ = false;
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
  int last_format_width_ = 0;
  int last_format_height_ = 0;

  bool has_caret_ = false;
  int caret_x_ = 0;
  int caret_y_ = 0;
  int caret_height_ = 0;
  bool has_caret_clip_ = false;
  struct rect caret_clip_{};

  bool is_mouse_down_ = false;
  bool drag_started_ = false;
  int press_doc_x_ = 0;
  int press_doc_y_ = 0;
  ::perception::window::MouseButton mouse_button_down_ =
      ::perception::window::MouseButton::Left;

  std::chrono::steady_clock::time_point last_click_time_{};
  int last_click_doc_x_ = 0;
  int last_click_doc_y_ = 0;
  int click_count_ = 0;

  bool shift_pressed_ = false;
  bool ctrl_pressed_ = false;
  bool alt_pressed_ = false;
};

// Synchronizes the navigation toolbar with the active tab's state.
void UpdateBrowserToolbar();

// Synchronizes the bottom status bar badges with download and console state.
void UpdateStatusBarBadges();

extern struct gui_window_table perception_window_table;
extern struct gui_search_table perception_search_table;

}  // namespace perception
}  // namespace netsurf

struct gui_window : public ::netsurf::perception::Window {
  using Window::Window;
};

#define NETSURF_LOCK \
  std::scoped_lock lock(::netsurf::perception::GetNetSurfMutex())
