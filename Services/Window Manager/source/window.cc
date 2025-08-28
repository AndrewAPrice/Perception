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

#include <iostream>
#include <map>
#include <set>

#include "compositor.h"
#include "highlighter.h"
#include "perception/devices/keyboard_device.h"
#include "perception/devices/mouse_listener.h"
#include "perception/draw.h"
#include "perception/linked_list.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/serialization/text_serializer.h"
#include "perception/services.h"
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

// Windows, mapped by their listeners.
std::map<BaseWindow::Client, std::shared_ptr<Window>> windows_by_listeners;

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
bool dragging_left_edge = false;
bool dragging_right_edge = false;
bool dragging_top_edge = false;
bool dragging_bottom_edge = false;

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
  if (area_to_fill.size.height <= 0 || area_to_fill.size.width <= 0) return;

  DrawOpaqueColor(area_to_fill, color);
}

void DrawAlphaWindowFramePart(const Rectangle& screen_area,
                              const Rectangle& frame_area, uint32 color) {
  auto area_to_fill = screen_area.Intersection(frame_area);
  if (area_to_fill.size.height <= 0 || area_to_fill.size.width <= 0) return;

  DrawAlphaBlendedColor(area_to_fill, color);
}

void StopDragging() {
  DisableHighlighter();
  dragging_window = nullptr;
  dragging_left_edge = false;
  dragging_right_edge = false;
  dragging_top_edge = false;
  dragging_bottom_edge = false;
}

void ValidateWindowBounds(Rectangle& bounds) {
  auto screen_size = GetScreenSize();
  for (int i = 0; i < 2; i++) {
    bounds.size[i] =
        std::max(kMinimumWindowSize, std::min(screen_size[i], bounds.size[i]));

    float min_position = -bounds.size[i] + kMinimumVisibleWindow;
    float max_position = screen_size[i] - kMinimumVisibleWindow;

    bounds.origin[i] =
        std::max(min_position, std::min(max_position, bounds.origin[i]));
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

  auto screen_size = GetScreenSize();
  window->screen_area_.size = {
      .width = request.desired_size.width > 0 ? request.desired_size.width
                                              : (screen_size.width * 3 / 4),
      .height = request.desired_size.height > 0 ? request.desired_size.height
                                                : (screen_size.height * 3 / 4)};
  window->CommonInit();

  // Center the new window in the middle of the screen.
  auto size_delta = screen_size - window->screen_area_.size;
  window->screen_area_.origin =
      Point{.x = size_delta.width / 2, .y = size_delta.height / 2};

  windows_by_listeners[request.window] = window;
  return window;
}

Window::~Window() {
  Hide();
  if (!window_listener_already_disappeared_) window_listener_.Closed({});
}

std::shared_ptr<Window> GetWindowWithListener(
    const BaseWindow::Client& window_listener) {
  auto window_itr = windows_by_listeners.find(window_listener);
  if (window_itr == windows_by_listeners.end()) return nullptr;
  return window_itr->second;
}

void Window::Focus() {
  if (IsFocused() || !IsVisible()) return;

  // There's a different focused window.
  if (focused_window) focused_window->Unfocus();

  focused_window = this;

  z_ordered_windows_.Remove(this);
  z_ordered_windows_.AddBack(this);

  Invalidate();

  if (!window_listener_already_disappeared_) window_listener_.GainedFocus({});

  // We now want to send keyboard events to this window.
  GetService<KeyboardDevice>().SetKeyboardListener(keyboard_listener_, {});
}

bool Window::IsFocused() const { return focused_window == this; }

void Window::Close() {
  Hide();
  std::weak_ptr<Window> weak_this = shared_from_this();

  window_listener_.StopNotifyingOnDisappearance(
      message_id_to_notify_on_window_disappearence_);

  Defer([weak_this]() {
    auto strong_this = weak_this.lock();
    if (strong_this) {
      // Remove the owner of the shared_ptr.
      windows_by_listeners.erase(strong_this->window_listener_);
    }
  });
}

void Window::UnfocusAllWindows() {
  if (focused_window) focused_window->Unfocus();
  GetService<KeyboardDevice>().SetKeyboardListener({}, {});
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
  if (IsDragging()) {
    bool resizing = false;

    auto new_screen_area = GetScreenArea();
    Point drag_offset = point - dragging_origin;
    if (dragging_left_edge) {
      new_screen_area.origin.x += drag_offset.x;
      new_screen_area.size.width -= drag_offset.x;
      resizing = true;
    } else if (dragging_right_edge) {
      new_screen_area.size.width += drag_offset.x;
      resizing = true;
    }

    if (dragging_top_edge) {
      new_screen_area.origin.y += drag_offset.y;
      new_screen_area.size.height -= drag_offset.y;
      resizing = true;
    } else if (dragging_bottom_edge) {
      new_screen_area.size.height += drag_offset.y;
      resizing = true;
    }

    if (!resizing) {
      // Handle dragging the entire window.
      new_screen_area.origin.x += drag_offset.x;
      new_screen_area.origin.y += drag_offset.y;
    }

    ValidateWindowBounds(new_screen_area);

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
    return true;
  }

  auto screen_area = GetScreenArea();
  Rectangle hit_area;

  bool check_for_begin_resizing = is_resizable_ && button_event &&
                                  button_event->is_pressed_down &&
                                  button_event->button == MouseButton::Left;
  if (check_for_begin_resizing) {
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
      if (mouse_listener_) mouse_listener_.MouseLeave({});
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

  if (check_for_begin_resizing) {
    // Check for the beginning of drags.
    if (point.x <= screen_area.MinX() + kDragBorder / 2.0f) {
      dragging_left_edge = true;
      dragging_window = this;
    } else if (point.x >= screen_area.MaxX() - kDragBorder / 2.0f) {
      dragging_right_edge = true;
      dragging_window = this;
    }

    if (point.y <= screen_area.MinY() + kDragBorder / 2.0f) {
      dragging_top_edge = true;
      dragging_window = this;
    } else if (point.y >= screen_area.MaxY() - kDragBorder / 2.0f) {
      dragging_bottom_edge = true;
      dragging_window = this;
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
      if (mouse_listener_) mouse_listener_.MouseEnter({});
    }

    std::optional<WindowButton> hovered_window_button;
    auto window_button_area = WindowButtonScreenArea();
    if (window_button_area.Contains(point)) {
      hovered_window_button = GetWindowButtonAtPoint(
          point.x - window_button_area.MinX(), is_resizable_);
    }

    if (hovered_window_button != hovered_window_button_) {
      hovered_window_button_ = hovered_window_button;
      InvalidateScreen(window_button_area);
    }

    Point local_point = point - screen_area.origin;
    if (button_event && !IsDragging()) {
      if (hovered_window_button && button_event->button == MouseButton::Left &&
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
      if (mouse_listener_) mouse_listener_.MouseClick(message, {});
    } else {
      // Hover event.
      MousePositionEvent message;
      message.x = local_point.x;
      message.y = local_point.y;
      if (mouse_listener_) mouse_listener_.MouseHover(message, {});
    }

  } else {
    if (IsHovering()) {
      if (mouse_listener_) mouse_listener_.MouseLeave({});
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
  // Draw the contents of the window.
  auto intersection = bounds.Intersection(screen_area);
  if (intersection.size.width >= 1 && intersection.size.height >= 1) {
    CopyOpaqueTexture(intersection, texture_id_,
                      intersection.origin - bounds.origin);
  }

  if (AreWindowButtonsVisible()) {
    auto button_screen_area = WindowButtonScreenArea();
    auto button_intersection = button_screen_area.Intersection(screen_area);
    if (button_intersection.size.width >= 1 &&
        button_intersection.size.height >= 1) {
      Point window_button_texture_offset =
          WindowButtonTextureOffset(is_resizable_, hovered_window_button_);
      CopyAlphaBlendedTexture(button_intersection, WindowButtonsTextureId(),
                              button_intersection.origin -
                                  button_screen_area.origin +
                                  window_button_texture_offset);
    }
  }
}

void Window::Invalidate() { return Invalidate(GetScreenAreaWithFrame()); }

void Window::Invalidate(const Rectangle& screen_area) {
  if (!IsVisible()) return Show();

  Rectangle screen_area_to_invalidate =
      screen_area.Intersection(GetScreenAreaWithFrame());
  if (screen_area_to_invalidate.Height() <= 0 ||
      screen_area_to_invalidate.Width() <= 0)
    return;

  InvalidateScreen(screen_area_to_invalidate);
}

void Window::InvalidateLocalArea(const Rectangle& window_area) {
  auto screen_area = window_area;
  screen_area.origin += screen_area_.origin;

  return Invalidate(screen_area);
}

void Window::StartDragging() {
  if (!IsFocused() || dragging_window != nullptr) return;

  dragging_window = this;
  dragging_left_edge = false;
  dragging_right_edge = false;
  dragging_top_edge = false;
  dragging_bottom_edge = false;

  dragging_origin = GetMousePosition();
}

Rectangle Window::GetScreenAreaWithFrame() const {
  auto screen_area = GetScreenArea();
  screen_area.origin -= Point{.x = kFrameThickness, .y = kFrameThickness};
  screen_area.size +=
      Size{.width = 2.0f * kFrameThickness + kDropFrameThickness,
           .height = 2.0f * kFrameThickness + kDropFrameThickness};
  return screen_area;
}

const Rectangle& Window::GetScreenArea() const { return screen_area_; }

void Window::SetTextureId(int texture_id) { texture_id_ = texture_id; }

void Window::CommonInit() {
  is_visible_ = false;
  ValidateWindowBounds(screen_area_);
  std::weak_ptr<Window> weak_self = shared_from_this();
  message_id_to_notify_on_window_disappearence_ =
      window_listener_.NotifyOnDisappearance([weak_self]() {
        auto strong_self = weak_self.lock();
        if (strong_self) {
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
    window_listener_.SetSize({screen_area_.Width(), screen_area_.Height()}, {});
}

void Window::Unfocus() {
  if (!IsFocused()) return;

  focused_window = nullptr;
  if (IsDragging()) StopDragging();
  if (IsHovering()) hovering_window = nullptr;

  if (!window_listener_already_disappeared_) window_listener_.LostFocus({});
}

bool Window::IsDragging() const { return dragging_window == this; }

bool Window::IsHovering() const { return hovering_window == this; }

Rectangle Window::WindowButtonScreenArea() const {
  Rectangle rect;
  rect.size = WindowButtonSize(is_resizable_);
  rect.origin.x = screen_area_.MaxX() - rect.size.width;
  rect.origin.y = screen_area_.origin.y;
  return rect;
}

bool Window::AreWindowButtonsVisible() const {
  return !hide_window_buttons_ || hovered_window_button_;
}

void Window::HandleWindowButtonClick() {
  if (!hovered_window_button_) return;

  switch (*hovered_window_button_) {
    case WindowButton::Close:
      Close();
      break;
    case WindowButton::Minimize:
      std::cout << "TODO: Implement minimize." << std::endl;
      break;
    case WindowButton::ToggleFullScreen:
      std::cout << "TODO: Implement toggle full screen." << std::endl;
      break;
  }
}
