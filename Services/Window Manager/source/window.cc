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

#include "window.h"

#include <cmath>
#include <iostream>
#include <map>
#include <set>

#include "compositor.h"
#include "highlighter.h"
#include "perception/devices/keyboard_device.h"
#include "perception/devices/mouse_listener.h"
#include "perception/draw.h"
#include "perception/linked_list.h"
#include "perception/loader.h"
#include "perception/permissions.h"
#include "perception/processes.h"
#include "perception/registry.h"
#include "perception/scheduler.h"
#include "perception/serialization/text_serializer.h"
#include "perception/services.h"
#include "perception/time.h"
#include "perception/ui/point.h"
#include "perception/ui/rectangle.h"
#include "perception/ui/size.h"
#include "perception/window/window_manager.h"
#include "screen.h"
#include "window_buttons.h"

using ::perception::Defer;
using ::perception::DrawXLine;
using ::perception::DrawXLineAlpha;
using ::perception::DrawYLine;
using ::perception::DrawYLineAlpha;
using ::perception::FillRectangle;
using ::perception::GetProcessName;
using ::perception::GetService;
using ::perception::LinkedList;
using ::perception::Status;
using ::perception::devices::KeyboardDevice;
using ::perception::devices::KeyboardListener;
using ::perception::devices::MouseButton;
using ::perception::devices::MouseClickEvent;
using ::perception::devices::MouseListener;
using ::perception::devices::MousePositionEvent;
using ::perception::serialization::SerializeToString;
using ::perception::ui::Point;
using ::perception::ui::Rectangle;
using ::perception::ui::Size;
using ::perception::window::BaseWindow;
using ::perception::window::CreateWindowRequest;

namespace {

constexpr int kMaxTitleLength = 50;
constexpr float kDragBorder = 6;

constexpr float kFrameThickness = 1;
constexpr float kDropFrameThickness = 2;

constexpr float kMinimumWindowSize = 64;
constexpr float kMinimumVisibleWindow = 8;
constexpr float kTitleBarHeight = 30;

// Windows, mapped by their listeners.
std::map<BaseWindow::Client, std::shared_ptr<Window>> windows_by_listeners;

struct ProcessWindowTracker {
  int window_count = 0;
  size_t last_zero_transition_id = 0;
};
std::map<::perception::ProcessId, ProcessWindowTracker> process_window_trackers;
size_t next_zero_transition_id = 0;

// Z-ordered windows, from back to front. Since LinkedLists can't hold
// shared_ptrs, the object must also be held by windows_by_listeners.
LinkedList<Window, &Window::z_ordering_linked_list_node_> z_ordered_windows_;

// The window that currently has focused.
Window* focused_window;

// Window that the mouse is currently over the contents of.
Window* hovering_window;

// The window being dragged.
Window* dragging_window;

// When dragging a dialog: offset
// When dragging a window: top left of the original title
Point dragging_origin;

// The edges being dragged. If all are false, then the entire window
// is being dragged.
bool dragging_min_edge[2] = {false, false};
bool dragging_max_edge[2] = {false, false};

int window_close_timeout_seconds = 10;
std::string ui_debugger_program = "UI Debugger";

// Returns whether the window listener can be used for creating a new window.
bool CanUseWindowListenerForNewWindow(BaseWindow::Client window_listener) {
  if (!window_listener) {
    std::cout << "Can't create a window without a window listener."
              << std::endl;
    return false;
  }

  auto itr = windows_by_listeners.find(window_listener);
  if (itr != windows_by_listeners.end()) {
    auto window = itr->second;
    std::cout << "Can't create a window with the window listener "
              << SerializeToString(window_listener) << " owned by \""
              << GetProcessName(window_listener.ServerProcessId())
              << "\" because it is already used by another window \""
              << window->GetTitle() << "\"." << std::endl;
    return false;
  }
  return true;
}

void DrawWindowFramePart(const Rectangle& screen_area,
                         const Rectangle& frame_area, uint32 color) {
  auto area_to_fill = screen_area.Intersection(frame_area);
  if (!area_to_fill) return;

  DrawOpaqueColor(*area_to_fill, color);
}

void DrawAlphaWindowFramePart(const Rectangle& screen_area,
                              const Rectangle& frame_area, uint32 color) {
  auto area_to_fill = screen_area.Intersection(frame_area);
  if (!area_to_fill) return;

  DrawAlphaBlendedColor(*area_to_fill, color);
}

void StopDragging() {
  DisableHighlighter();
  dragging_window = nullptr;
  for (int d = 0; d < 2; d++) {
    dragging_min_edge[d] = false;
    dragging_max_edge[d] = false;
  }
}

void ValidateWindowBounds(Rectangle& bounds) {
  auto screen_size = GetScreenSize();
  for (int i = 0; i < 2; i++) {
    bounds.size[i] =
        std::max(kMinimumWindowSize,
                 std::min(screen_size[i], std::round(bounds.size[i])));

    float min_position = -bounds.size[i] + kMinimumVisibleWindow;
    float max_position = screen_size[i] - kMinimumVisibleWindow;

    bounds.origin[i] = std::max(
        min_position, std::min(max_position, std::round(bounds.origin[i])));
  }
}

}  // namespace

StatusOr<std::shared_ptr<Window>> Window::CreateWindow(
    const CreateWindowRequest& request) {
  if (!CanUseWindowListenerForNewWindow(request.window))
    return Status::INVALID_ARGUMENT;

  auto window = std::make_shared<Window>();
  window->title_ = request.title;
  if (window->title_.size() > kMaxTitleLength) {
    window->title_ = window->title_.substr(0, kMaxTitleLength);
  }
  window->is_resizable_ = request.is_resizable;
  window->hide_window_buttons_ = request.hide_window_buttons;
  window->window_listener_ = request.window;
  window->keyboard_listener_ = request.keyboard_listener;
  window->mouse_listener_ = request.mouse_listener;

  window->screen_area_.size = {request.desired_size.width,
                               request.desired_size.height};

  window->CommonInit();

  windows_by_listeners[request.window] = window;
  if (request.window) {
    ::perception::ProcessId pid = request.window.ServerProcessId();
    if (pid != 0) process_window_trackers[pid].window_count++;
  }
  return window;
}

Window::~Window() {
  Hide();
  if (!window_listener_already_disappeared_) window_listener_.Closed(nullptr);
}

std::shared_ptr<Window> GetWindowWithListener(
    const BaseWindow::Client& window_listener) {
  auto window_itr = windows_by_listeners.find(window_listener);
  if (window_itr == windows_by_listeners.end()) return nullptr;
  return window_itr->second;
}

void Window::SetCursor(::perception::window::Cursor cursor) {
  if (cursor_ == cursor) return;
  cursor_ = cursor;
  InvalidateMouse();
}

::perception::window::Cursor Window::GetCursor() const { return cursor_; }

::perception::window::Cursor Window::GetCursorAtPoint(const Point& point) {
  ::perception::window::Cursor cursor = ::perception::window::Cursor::Pointer;

  if (dragging_window) {
    bool min_x = dragging_min_edge[0];
    bool max_x = dragging_max_edge[0];
    bool min_y = dragging_min_edge[1];
    bool max_y = dragging_max_edge[1];
    if ((min_x || max_x) || (min_y || max_y)) {
      // We are resizing!
      if ((min_x && min_y) || (max_x && max_y)) {
        return ::perception::window::Cursor::ResizeDiagonalTopLeftBottomRight;
      } else if ((min_x && max_y) || (max_x && min_y)) {
        return ::perception::window::Cursor::ResizeDiagonalTopRightBottomLeft;
      } else if (min_x || max_x) {
        return ::perception::window::Cursor::ResizeHorizontal;
      } else {
        return ::perception::window::Cursor::ResizeVertical;
      }
    }
    return dragging_window->GetCursor();
  }

  (void)Window::ForEachFrontToBackWindow([&](Window& window) {
    if (!window.IsVisible()) return false;

    auto screen_area = window.GetScreenArea();
    Rectangle hit_area;

    if (window.is_fullscreen_) {
      hit_area = screen_area;
    } else if (window.is_resizable_) {
      hit_area = {.origin = screen_area.origin -
                            Point{kDragBorder / 2.0f, kDragBorder / 2.0f},
                  .size = screen_area.size + Size{kDragBorder, kDragBorder}};
    } else {
      hit_area = {.origin = screen_area.origin -
                            Point{kFrameThickness, kFrameThickness},
                  .size = screen_area.size +
                          Size{kFrameThickness * 2.0f, kFrameThickness * 2.0f}};
    }

    if (!hit_area.Contains(point)) return false;  // Keep looking.

    if (window.IsDebugging()) {
      cursor = ::perception::window::Cursor::Pointer;
      return true;
    }

    // The point is over this window!
    // 1. Check if it's over the resizable edge of the window.
    if (window.is_resizable_ && !window.is_fullscreen_) {
      bool is_min_x = point.x <= screen_area.origin.x + kDragBorder / 2.0f;
      bool is_max_x = point.x >= screen_area.origin.x + screen_area.size.width -
                                     kDragBorder / 2.0f;
      bool is_min_y = point.y <= screen_area.origin.y + kDragBorder / 2.0f;
      bool is_max_y = point.y >= screen_area.origin.y +
                                     screen_area.size.height -
                                     kDragBorder / 2.0f;

      if (is_min_x || is_max_x || is_min_y || is_max_y) {
        // It's on a resizable edge!
        if ((is_min_x && is_min_y) || (is_max_x && is_max_y)) {
          cursor =
              ::perception::window::Cursor::ResizeDiagonalTopLeftBottomRight;
        } else if ((is_min_x && is_max_y) || (is_max_x && is_min_y)) {
          cursor =
              ::perception::window::Cursor::ResizeDiagonalTopRightBottomLeft;
        } else if (is_min_x || is_max_x) {
          cursor = ::perception::window::Cursor::ResizeHorizontal;
        } else {
          cursor = ::perception::window::Cursor::ResizeVertical;
        }
        return true;  // Stop searching
      }
    }

    // 2. Check if it's over one of the window's system buttons.
    if (window.AreWindowButtonsVisible()) {
      auto window_button_area = window.WindowButtonScreenArea();
      if (window_button_area.Contains(point)) {
        cursor = ::perception::window::Cursor::Poke;
        return true;
      }
    }

    // 3. Otherwise, it's over the window's contents.
    cursor = window.GetCursor();
    return true;  // Stop searching
  });

  return cursor;
}

bool Window::IsDebugging() const { return is_debugging_; }

void Window::SetIsDebugging(bool is_debugging) {
  if (is_debugging_ == is_debugging) return;
  is_debugging_ = is_debugging;
  if (IsFocused()) {
    if (is_debugging_) {
      GetService<KeyboardDevice>().SetKeyboardListener({}, nullptr);
    } else {
      GetService<KeyboardDevice>().SetKeyboardListener(keyboard_listener_,
                                                       nullptr);
    }
  }
  Invalidate();
}

std::shared_ptr<Window> Window::GetDebuggingWindowForSender(
    ::perception::ProcessId sender) {
  for (auto& [client, window] : windows_by_listeners) {
    if (client.ServerProcessId() == sender && window->IsDebugging())
      return window;
  }
  return nullptr;
}

void Window::Focus() {
  if (IsFocused() || !IsVisible()) return;

  // There's a different focused window.
  if (focused_window) focused_window->Unfocus();

  focused_window = this;

  z_ordered_windows_.Remove(this);
  z_ordered_windows_.AddBack(this);

  Invalidate();

  if (!window_listener_already_disappeared_) {
    ::perception::SetFocusedProcess(window_listener_.ServerProcessId());
    window_listener_.GainedFocus(nullptr);
  }

  // We now want to send keyboard events to this window.
  if (is_debugging_) {
    GetService<KeyboardDevice>().SetKeyboardListener({}, nullptr);
  } else {
    GetService<KeyboardDevice>().SetKeyboardListener(keyboard_listener_,
                                                     nullptr);
  }
}

bool Window::IsFocused() const { return focused_window == this; }

void Window::Close() {
  if (is_closed_) return;
  is_closed_ = true;

  Hide();
  std::weak_ptr<Window> weak_this = shared_from_this();

  window_listener_.StopNotifyingOnDisappearance(
      message_id_to_notify_on_window_disappearence_);

  if (window_listener_) {
    ::perception::ProcessId pid = window_listener_.ServerProcessId();
    if (pid != 0) {
      auto itr = process_window_trackers.find(pid);
      if (itr != process_window_trackers.end()) {
        itr->second.window_count--;
        if (itr->second.window_count <= 0) {
          next_zero_transition_id++;
          size_t transition_id = next_zero_transition_id;
          itr->second.last_zero_transition_id = transition_id;

          ::perception::AfterDuration(
              std::chrono::seconds(window_close_timeout_seconds),
              [pid, transition_id]() {
                if (!::perception::DoesProcessExist(pid)) {
                  process_window_trackers.erase(pid);
                  return;
                }
                auto track_itr = process_window_trackers.find(pid);
                if (track_itr == process_window_trackers.end() ||
                    track_itr->second.window_count > 0 ||
                    track_itr->second.last_zero_transition_id !=
                        transition_id) {
                  return;
                }
                ::perception::DoesProcessHavePermission(
                    pid,
                    ::perception::Permission::
                        CanContinueRunningAfterWindowsClose,
                    [pid, transition_id](bool has_permission) {
                      auto track_itr2 = process_window_trackers.find(pid);
                      if (track_itr2 != process_window_trackers.end() &&
                          track_itr2->second.window_count == 0 &&
                          track_itr2->second.last_zero_transition_id ==
                              transition_id) {
                        process_window_trackers.erase(track_itr2);
                      }
                      if (!has_permission &&
                          ::perception::DoesProcessExist(pid)) {
                        ::perception::TerminateProcesss(pid);
                      }
                    });
              });
        }
      }
    }
  }

  Defer([weak_this]() {
    // Remove the owner of the shared_ptr.
    if (auto strong_this = weak_this.lock())
      windows_by_listeners.erase(strong_this->window_listener_);
  });
}

void Window::UnfocusAllWindows() {
  if (focused_window) focused_window->Unfocus();
  GetService<KeyboardDevice>().SetKeyboardListener({}, nullptr);
}

void Window::EnsureWindowsAreOnScreen() {
  (void)ForEachFrontToBackWindow([](Window& window) {
    if (window.is_fullscreen_) {
      auto screen_size = GetScreenSize();
      if (window.screen_area_.size != screen_size) {
        window.screen_area_ = {0, 0, screen_size.width, screen_size.height};
        window.Resized();
      }
      ValidateWindowBounds(window.previous_screen_area_);
      return false;
    }

    auto old_area = window.GetScreenAreaWithFrame();
    ValidateWindowBounds(window.screen_area_);
    if (old_area != window.GetScreenAreaWithFrame()) {
      window.Resized();
    }
    return false;
  });
}

bool Window::ExitFullScreen() {
  return ForEachFrontToBackWindow([](Window& window) {
    if (window.is_fullscreen_) {
      window.ToggleFullScreen();
      return true;
    }
    return false;
  });
}

std::string_view Window::GetTitle() const { return title_; }

bool Window::ForEachFrontToBackWindow(
    const std::function<bool(Window&)>& on_each_window) {
  for (auto itr = z_ordered_windows_.rbegin(); itr != z_ordered_windows_.rend();
       ++itr) {
    if (on_each_window(**itr)) return true;
  }
  return false;
}

bool Window::ForEachBackToFrontWindow(
    const std::function<bool(Window&)>& on_each_window) {
  for (auto itr = z_ordered_windows_.begin(); itr != z_ordered_windows_.end();
       ++itr) {
    if (on_each_window(**itr)) return true;
  }
  return false;
}

bool Window::MouseEvent(const Point& point,
                        std::optional<MouseButtonEvent> button_event) {
  if (!IsVisible()) return false;

  if (IsDragging()) {
    bool resizing = false;

    auto original_screen_area = GetScreenArea();
    auto new_screen_area = original_screen_area;
    Point drag_offset = point - dragging_origin;
    for (int d = 0; d < 2; d++) {
      if (dragging_min_edge[d]) {
        float new_size = original_screen_area.size[d] - drag_offset[d];
        new_screen_area.size[d] = std::max(kMinimumWindowSize, new_size);
        new_screen_area.origin[d] = original_screen_area.origin[d] +
                                    original_screen_area.size[d] -
                                    new_screen_area.size[d];
        resizing = true;
      } else if (dragging_max_edge[d]) {
        new_screen_area.size[d] += drag_offset[d];
        resizing = true;
      }
    }

    if (!resizing) {
      // Handle dragging the entire window.
      new_screen_area.origin += drag_offset;
    }

    ValidateWindowBounds(new_screen_area);

    if (resizing) {
      if (button_event && !button_event->is_pressed_down &&
          button_event->button == MouseButton::Left) {
        // Released the drag.
        StopDragging();

        if (screen_area_ != new_screen_area) {
          bool resized = screen_area_.size != new_screen_area.size;

          // The bounds have changed. Update the frame and invalidate the
          // screen where both the old frame and the new frames are.
          auto old_area_with_frame = GetScreenAreaWithFrame();
          screen_area_ = new_screen_area;
          auto new_area_with_frame = GetScreenAreaWithFrame();

          if (resized) Resized();
          InvalidateScreen(old_area_with_frame.Union(new_area_with_frame));
        }
      } else {
        // Still dragging.
        new_screen_area.origin -=
            Point{.x = kFrameThickness, .y = kFrameThickness};
        new_screen_area.size += Size{.width = 2.0f * kFrameThickness,
                                     .height = 2.0f * kFrameThickness};
        SetHighlighter(new_screen_area);
      }
    } else {
      if (button_event && !button_event->is_pressed_down &&
          button_event->button == MouseButton::Left) {
        // Released the drag.
        StopDragging();
      } else {
        // Still dragging. Update the screen area immediately!
        if (screen_area_ != new_screen_area) {
          auto old_area_with_frame = GetScreenAreaWithFrame();
          screen_area_ = new_screen_area;
          auto new_area_with_frame = GetScreenAreaWithFrame();
          InvalidateScreen(old_area_with_frame.Union(new_area_with_frame));
          dragging_origin = point;
        }
      }
    }
    return true;
  }

  auto screen_area = GetScreenArea();
  Rectangle hit_area;

  bool check_for_begin_resizing = !is_debugging_ && is_resizable_ &&
                                  !is_fullscreen_ && button_event &&
                                  button_event->is_pressed_down &&
                                  button_event->button == MouseButton::Left;
  if (is_fullscreen_) {
    hit_area = screen_area;
  } else if (check_for_begin_resizing) {
    hit_area = {.origin = screen_area.origin -
                          Point{kDragBorder / 2.0f, kDragBorder / 2.0f},
                .size = screen_area.size + Size{kDragBorder, kDragBorder}};
  } else {
    hit_area = {
        .origin = screen_area.origin - Point{kFrameThickness, kFrameThickness},
        .size = screen_area.size +
                Size{kFrameThickness * 2.0f, kFrameThickness * 2.0f}};
  }

  if (!hit_area.Contains(point)) {
    // Not even in the hit area.
    if (IsHovering()) {
      if (mouse_listener_) mouse_listener_.MouseLeave(nullptr);
      hovering_window = nullptr;
    }
    if (hovered_window_button_) {
      hovered_window_button_ = std::nullopt;
      InvalidateScreen(WindowButtonScreenArea());
    }
    return false;
  }

  if (button_event && button_event->is_pressed_down && !IsFocused()) {
    Focus();
  }

  if (is_debugging_) {
    if (button_event && button_event->is_pressed_down) {
      StartDragging();
      if (IsDragging()) {
        dragging_origin = point;
      }
    }
    return true;
  }

  if (check_for_begin_resizing) {
    // Check for the beginning of drags.
    for (int d = 0; d < 2; d++) {
      if (point[d] <= screen_area.origin[d] + kDragBorder / 2.0f) {
        dragging_min_edge[d] = true;
        dragging_window = this;
      } else if (point[d] >= screen_area.origin[d] + screen_area.size[d] -
                                 kDragBorder / 2.0f) {
        dragging_max_edge[d] = true;
        dragging_window = this;
      }
    }

    if (IsDragging()) {
      // Starting a drag!
      dragging_origin = point;

      screen_area.origin -= Point{.x = kFrameThickness, .y = kFrameThickness};
      screen_area.size += Size{.width = 2.0f * kFrameThickness,
                               .height = 2.0f * kFrameThickness};

      SetHighlighter(screen_area);
    }
  }

  if (screen_area.Contains(point)) {
    if (!IsHovering()) {
      hovering_window = this;
      if (mouse_listener_) mouse_listener_.MouseEnter(nullptr);
    }

    std::optional<WindowButton> hovered_window_button;
    if (!is_fullscreen_) {
      auto window_button_area = WindowButtonScreenArea();
      if (window_button_area.Contains(point)) {
        hovered_window_button = GetWindowButtonAtPoint(
            point.x - window_button_area.MinX(), is_resizable_);
      }
    }

    if (hovered_window_button != hovered_window_button_) {
      hovered_window_button_ = hovered_window_button;
      InvalidateScreen(WindowButtonScreenArea());
    }

    Point local_point = point - screen_area.origin;
    if (button_event && !IsDragging()) {
      if (!is_fullscreen_ && hovered_window_button &&
          button_event->button == MouseButton::Left &&
          button_event->is_pressed_down) {
        HandleWindowButtonClick();
        return true;
      }
      // Click event.
      MouseClickEvent message;
      message.position.x = local_point.x;
      message.position.y = local_point.y;
      message.button.button = button_event->button;
      message.button.is_pressed_down = button_event->is_pressed_down;
      if (mouse_listener_) mouse_listener_.MouseClick(message, nullptr);
    } else {
      // Hover event.
      MousePositionEvent message;
      message.x = local_point.x;
      message.y = local_point.y;
      if (mouse_listener_) mouse_listener_.MouseHover(message, nullptr);
    }

  } else {
    if (IsHovering()) {
      if (mouse_listener_) mouse_listener_.MouseLeave(nullptr);
      hovering_window = nullptr;
    }
    if (hovered_window_button_) {
      hovered_window_button_ = std::nullopt;
      InvalidateScreen(WindowButtonScreenArea());
    }
  }

  return true;
}

void Window::Draw(const Rectangle& screen_area) {
  if (!IsVisible()) return;
  if (!screen_area.Intersects(GetScreenAreaWithFrame())) return;
  auto bounds = GetScreenArea();

  if (!is_fullscreen_) {
    // Draw the frame.
    float max_x = bounds.MaxX();
    float max_y = bounds.MaxY();
    float horizontal_frame_width = bounds.size.width + 2;
    float vertical_frame_height = bounds.size.height;
    // Top frame.
    DrawWindowFramePart(
        screen_area,
        {.origin = {.x = bounds.origin.x - 1, .y = bounds.origin.y - 1},
         .size = {.width = horizontal_frame_width, .height = 1}},
        WINDOW_BORDER_COLOUR);

    // Left frame.
    DrawWindowFramePart(
        screen_area,
        {.origin = {.x = bounds.origin.x - 1, .y = bounds.origin.y},
         .size = {.width = 1, .height = vertical_frame_height}},
        WINDOW_BORDER_COLOUR);

    // Bottom frame, with shadows.
    Rectangle bottom_frame = {
        .origin = {.x = bounds.origin.x - 1, .y = max_y},
        .size = {.width = horizontal_frame_width, .height = 1}};
    DrawWindowFramePart(screen_area, bottom_frame, WINDOW_BORDER_COLOUR);
    bottom_frame.origin += {.x = 1, .y = 1};
    DrawAlphaWindowFramePart(screen_area, bottom_frame, WINDOW_SHADOW_1);
    bottom_frame.origin += {.x = 1, .y = 1};
    DrawAlphaWindowFramePart(screen_area, bottom_frame, WINDOW_SHADOW_2);

    // Right frame, with shadows.
    Rectangle right_frame = {
        .origin = {.x = max_x, .y = bounds.origin.y},
        .size = {.width = 1, .height = vertical_frame_height}};
    DrawWindowFramePart(screen_area, right_frame, WINDOW_BORDER_COLOUR);
    right_frame.origin.x += 1;
    right_frame.size.height += 1;
    DrawAlphaWindowFramePart(screen_area, right_frame, WINDOW_SHADOW_1);
    right_frame.origin += {.x = 1, .y = 1};
    DrawAlphaWindowFramePart(screen_area, right_frame, WINDOW_SHADOW_2);
  }
  // Draw the contents of the window.
  auto intersection = bounds.Intersection(screen_area);
  if (intersection) {
    CopyOpaqueTexture(*intersection, texture_id_,
                      intersection->origin - bounds.origin);
    if (is_debugging_) {
      DrawAlphaBlendedColor(*intersection, DEBUGGING_TINT);
    }
  }

  if (AreWindowButtonsVisible()) {
    auto button_screen_area = WindowButtonScreenArea();
    auto button_intersection = button_screen_area.Intersection(screen_area);
    if (button_intersection) {
      Point window_button_texture_offset =
          WindowButtonTextureOffset(is_resizable_, hovered_window_button_);
      CopyAlphaBlendedTexture(*button_intersection, WindowButtonsTextureId(),
                              button_intersection->origin -
                                  button_screen_area.origin +
                                  window_button_texture_offset);
    }
  }
}

void Window::Invalidate() { return Invalidate(GetScreenAreaWithFrame()); }

void Window::Invalidate(const Rectangle& screen_area) {
  if (!IsVisible()) return Show();

  auto screen_area_to_invalidate =
      screen_area.Intersection(GetScreenAreaWithFrame());
  if (!screen_area_to_invalidate) return;

  InvalidateScreen(*screen_area_to_invalidate);
}

void Window::InvalidateLocalArea(const Rectangle& window_area) {
  auto screen_area = window_area;
  screen_area.origin += screen_area_.origin;

  return Invalidate(screen_area);
}

void Window::StartDragging() {
  if (!IsFocused() || dragging_window != nullptr) return;

  dragging_window = this;
  for (int d = 0; d < 2; d++) {
    dragging_min_edge[d] = false;
    dragging_max_edge[d] = false;
  }

  dragging_origin = GetMousePosition();

  if (is_fullscreen_) {
    is_fullscreen_ = false;
    auto screen_size = GetScreenSize();
    screen_area_.size = previous_screen_area_.size;

    for (int d = 0; d < 2; d++) {
      if (dragging_origin[d] < screen_size[d] / 2.0f) {
        float anchor_offset =
            std::min(dragging_origin[d], previous_screen_area_.size[d] * 0.5f);
        screen_area_.origin[d] = dragging_origin[d] - anchor_offset;
      } else {
        float offset = screen_size[d] - dragging_origin[d];
        float anchor_offset =
            std::min(offset, previous_screen_area_.size[d] * 0.5f);
        screen_area_.origin[d] =
            dragging_origin[d] + anchor_offset - previous_screen_area_.size[d];
      }
    }

    ValidateWindowBounds(screen_area_);
    InvalidateScreen(Rectangle{.origin = {0, 0}, .size = screen_size});
    Resized();
  }
}

Rectangle Window::GetScreenAreaWithFrame() const {
  if (is_fullscreen_) return screen_area_;
  auto screen_area = GetScreenArea();
  screen_area.origin -= Point{.x = kFrameThickness, .y = kFrameThickness};
  screen_area.size +=
      Size{.width = 2.0f * kFrameThickness + kDropFrameThickness,
           .height = 2.0f * kFrameThickness + kDropFrameThickness};
  return screen_area;
}

const Rectangle& Window::GetScreenArea() const { return screen_area_; }

void Window::SetTextureId(int texture_id) {
  texture_id_ = texture_id;
  if (!is_visible_) {
    Show();
  } else {
    Invalidate();
  }
}

void Window::SetSize(const ::perception::window::Size& size) {
  auto screen_size = GetScreenSize();
  float new_w = std::max(kMinimumWindowSize,
                         std::min(screen_size.width, std::round(size.width)));
  float new_h = std::max(kMinimumWindowSize,
                         std::min(screen_size.height, std::round(size.height)));

  if (is_fullscreen_) {
    previous_screen_area_.size.width = new_w;
    previous_screen_area_.size.height = new_h;
    ValidateWindowBounds(previous_screen_area_);
    return;
  }

  if (screen_area_.size.width == new_w && screen_area_.size.height == new_h)
    return;

  auto old_area_with_frame = GetScreenAreaWithFrame();

  screen_area_.size.width = new_w;
  screen_area_.size.height = new_h;

  ValidateWindowBounds(screen_area_);

  auto new_area_with_frame = GetScreenAreaWithFrame();

  Resized();
  InvalidateScreen(old_area_with_frame.Union(new_area_with_frame));
}

void Window::CommonInit() {
  is_visible_ = false;
  is_fullscreen_ = false;
  is_debugging_ = false;
  is_closed_ = false;
  cursor_ = ::perception::window::Cursor::Pointer;

  auto screen_size = GetScreenSize();
  screen_area_.size = {.width = screen_area_.size.width >= 1.0f
                                    ? screen_area_.size.width
                                    : (screen_size.width * 0.75f),
                       .height = screen_area_.size.height >= 1.0f
                                     ? screen_area_.size.height
                                     : (screen_size.height * 0.75f)};

  // Center the new window in the middle of the screen.
  auto size_delta = screen_size - screen_area_.size;
  screen_area_.origin =
      Point{.x = size_delta.width / 2.0f, .y = size_delta.height / 2.0f};

  ValidateWindowBounds(screen_area_);
  std::weak_ptr<Window> weak_self = shared_from_this();
  message_id_to_notify_on_window_disappearence_ =
      window_listener_.NotifyOnDisappearance([weak_self]() {
        if (auto strong_self = weak_self.lock()) {
          strong_self->window_listener_already_disappeared_ = true;
          strong_self->Close();
        }
      });
}

void Window::Show() {
  if (IsVisible()) return;

  // There needs to be a texture to draw.
  if (texture_id_ == 0) return;

  z_ordered_windows_.AddBack(this);
  is_visible_ = true;

  Focus();

  InvalidateScreen(GetScreenAreaWithFrame());
}

void Window::Hide() {
  if (!IsVisible()) return;

  if (is_fullscreen_) {
    is_fullscreen_ = false;
    screen_area_ = previous_screen_area_;
    ValidateWindowBounds(screen_area_);
    InvalidateScreen(Rectangle{.origin = {0, 0}, .size = GetScreenSize()});
  }

  if (IsDragging()) StopDragging();
  if (IsHovering()) hovering_window = nullptr;

  if (IsFocused()) {
    Window* previous_window = z_ordered_windows_.PreviousItem(this);
    if (previous_window) {
      previous_window->Focus();
    } else {
      UnfocusAllWindows();
    }
  }
  z_ordered_windows_.Remove(this);
  InvalidateScreen(GetScreenAreaWithFrame());
  is_visible_ = false;
}

void Window::Resized() {
  if (!window_listener_already_disappeared_)
    window_listener_.SetSize({screen_area_.Width(), screen_area_.Height()},
                             nullptr);
}

void Window::Unfocus() {
  if (!IsFocused()) return;

  focused_window = nullptr;
  ::perception::SetFocusedProcess(0);
  if (IsDragging()) StopDragging();
  if (IsHovering()) hovering_window = nullptr;

  if (!window_listener_already_disappeared_)
    window_listener_.LostFocus(nullptr);
}

bool Window::IsDragging() const { return dragging_window == this; }

bool Window::IsHovering() const { return hovering_window == this; }

Rectangle Window::WindowButtonScreenArea() const {
  Rectangle rect;
  rect.size = WindowButtonSize(is_resizable_);
  rect.origin.x = screen_area_.MaxX() - rect.size.width;
  rect.origin.y =
      screen_area_.origin.y + (kTitleBarHeight - rect.size.height) / 2.0f;
  return rect;
}

bool Window::AreWindowButtonsVisible() const {
  if (is_fullscreen_ || is_debugging_) return false;
  return !hide_window_buttons_ || hovered_window_button_;
}

void Window::HandleWindowButtonClick() {
  if (!hovered_window_button_) return;

  switch (*hovered_window_button_) {
    case WindowButton::Close:
      Close();
      break;
    case WindowButton::Debug: {
      SetIsDebugging(true);
      std::weak_ptr<Window> weak_this = shared_from_this();
      window_listener_.GetUiHierarchy(
          [weak_this](
              StatusOr<::perception::window::DebugUiHierarchy> hierarchy) {
            if (auto window = weak_this.lock()) {
              window->SetIsDebugging(false);
              if (hierarchy) {
                ::perception::LoadApplicationRequest request;
                request.name = ui_debugger_program;
                request.arguments = {
                    hierarchy->json,
                    std::to_string(window->window_listener_.ServerProcessId()),
                    std::to_string(window->window_listener_.ServiceId())};
                ::perception::GetService<::perception::Loader>()
                    .LaunchApplication(request, nullptr);
              }
            }
          });
      break;
    }
    case WindowButton::ToggleFullScreen:
      ToggleFullScreen();
      break;
  }
}

void Window::ToggleFullScreen() {
  if (is_fullscreen_) {
    // Exit full screen.
    is_fullscreen_ = false;
    screen_area_ = previous_screen_area_;
    ValidateWindowBounds(screen_area_);
    InvalidateScreen(Rectangle{.origin = {0, 0}, .size = GetScreenSize()});
    Resized();
  } else {
    // Enter full screen.
    if (!is_resizable_) return;
    previous_screen_area_ = screen_area_;
    is_fullscreen_ = true;
    screen_area_ = {.origin = {0, 0}, .size = GetScreenSize()};
    hovered_window_button_ = std::nullopt;
    InvalidateScreen(Rectangle{.origin = {0, 0}, .size = GetScreenSize()});
    Resized();
  }
}

void UpdateWindowCloseTimeout() {
  auto val_or = ::perception::GetRegistryValue("windowCloseTimeoutSeconds");
  if (val_or.Ok()) {
    window_close_timeout_seconds =
        static_cast<int>(val_or->IntegerValue().value_or(10));
  } else {
    window_close_timeout_seconds = 10;
  }
}

void UpdateUiDebuggerProgram() {
  auto val_or = ::perception::GetRegistryValue("uiDebuggerProgram");
  if (val_or.Ok() &&
      val_or->GetType() == ::perception::serialization::Value::Type::STRING) {
    auto str = val_or->StringValue();
    if (str && !str->empty()) {
      ui_debugger_program = std::string(*str);
      return;
    }
  }
  ui_debugger_program = "UI Debugger";
}
