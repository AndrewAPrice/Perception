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

#include "perception/ui/components/ui_window.h"

#include <iostream>
#include <mutex>
#include <set>

#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkGraphics.h"
#include "include/core/SkSurface.h"
#include "perception/debug.h"
#include "perception/draw.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/ui/color_space.h"
#include "perception/ui/components/focusable.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/ui/theme.h"
#include "perception/window/keyboard_key_event.h"
#include "perception/window/mouse_button_event.h"
#include "perception/window/mouse_click_event.h"
#include "perception/window/mouse_hover_event.h"
#include "perception/window/mouse_move_event.h"
#include "perception/window/mouse_scroll_event.h"
#include "perception/window/rectangle.h"
#include "perception/window/window.h"
#include "perception/window/window_delegate.h"
#include "perception/window/window_draw_buffer.h"
#include "perception/window/window_manager.h"

using ::perception::window::MouseButton;

namespace perception {
template class UniqueIdentifiableType<ui::components::UiWindow>;
namespace ui {
namespace components {

namespace {

// Number of logical pixels to scroll per mouse wheel step.
constexpr float kScrollPixelsPerWheelStep = 40.0f;

// Default logical height of the system window buttons area.
constexpr float kDefaultSystemButtonHeight = 24.0f;

// Translate a screen-space point to be node-space.
Point ScreenPointToNodePoint(std::shared_ptr<Node> target, Point point) {
  if (!target) return point;
  return point - target->GetAbsolutePosition();
}

std::recursive_mutex& GetGlobalColorSpaceMutex() {
  static std::recursive_mutex mutex;
  return mutex;
}

sk_sp<SkColorSpace>& GetGlobalColorSpaceRef() {
  static sk_sp<SkColorSpace> color_space;
  return color_space;
}

float& GetGlobalScaleRef() {
  static float scale = 1.0f;
  return scale;
}

std::set<UiWindow*>& GetOpenUiWindows() {
  static std::set<UiWindow*> windows;
  return windows;
}

bool global_listener_initialized = false;

class GlobalWindowManagerEnvironmentListener
    : public ::perception::window::WindowManagerEnvironmentListener::Server {
 public:
  Status WindowManagerEnvironmentChanged(
      const ::perception::window::WindowManagerEnvironmentChangedNotification&
          notification) override {
    auto new_color_space = DeserializeColorSpace(notification.color_space);
    UiWindow::OnGlobalEnvironmentChanged(new_color_space, notification.scale);
    return Status::OK;
  }
};

std::unique_ptr<GlobalWindowManagerEnvironmentListener>&
GetGlobalEnvironmentListenerServer() {
  static std::unique_ptr<GlobalWindowManagerEnvironmentListener> server;
  return server;
}

void EnsureGlobalColorSpaceInitialized() {
  {
    std::scoped_lock lock(GetGlobalColorSpaceMutex());
    if (global_listener_initialized) return;
    global_listener_initialized = true;
  }

  auto listener_server =
      std::make_unique<GlobalWindowManagerEnvironmentListener>();

  sk_sp<SkColorSpace> fetched_color_space;
  float fetched_scale = 1.0f;
  auto window_manager = ::perception::FindFirstInstanceOfService<
      ::perception::window::WindowManager>();
  if (window_manager) {
    auto response_or = window_manager->GetEnvironment();
    if (response_or.Ok()) {
      fetched_color_space = DeserializeColorSpace(response_or->color_space);
      fetched_scale = response_or->scale;
    }
  }
  if (!fetched_color_space) {
    fetched_color_space = SkColorSpace::MakeSRGB();
  }
  if (fetched_scale < 0.5f) fetched_scale = 0.5f;

  std::scoped_lock lock(GetGlobalColorSpaceMutex());
  GetGlobalEnvironmentListenerServer() = std::move(listener_server);
  if (!GetGlobalColorSpaceRef()) {
    GetGlobalColorSpaceRef() = fetched_color_space;
  }
  GetGlobalScaleRef() = fetched_scale;
}

sk_sp<SkColorSpace> GetGlobalColorSpace() {
  EnsureGlobalColorSpaceInitialized();
  std::scoped_lock lock(GetGlobalColorSpaceMutex());
  return GetGlobalColorSpaceRef();
}

}  // namespace

UiWindow::UiWindow()
    : invalidated_(false),
      created_(false),
      is_resizable_(false),
      fit_content_width_(false),
      fit_content_height_(false),
      is_drawing_(false),
      full_repaint_needed_(true),
      dirty_rect_(std::nullopt),
      last_logical_width_(0.0f),
      last_logical_height_(0.0f),
      background_color_(kBackgroundWindowColor),
      next_focus_changed_handler_id_(1),
      pixel_data_(nullptr),
      buffer_width_(0),
      buffer_height_(0),
      pressed_mouse_buttons_(0),
      last_mouse_position_({.x = 0.0f, .y = 0.0f}) {
  static std::once_flag skia_init_flag;
  std::call_once(skia_init_flag, []() { SkGraphics::Init(); });
  EnsureGlobalColorSpaceInitialized();
  std::scoped_lock lock(GetGlobalColorSpaceMutex());
  GetOpenUiWindows().insert(this);
}

UiWindow::~UiWindow() {
  Close();
  std::scoped_lock lock(GetGlobalColorSpaceMutex());
  GetOpenUiWindows().erase(this);
}

void UiWindow::SetColorSpace(sk_sp<SkColorSpace> color_space) {
  std::scoped_lock lock(window_mutex_);
  custom_color_space_ = color_space;
  skia_surface_ = nullptr;
  InvalidateRender();
}

sk_sp<SkColorSpace> UiWindow::GetColorSpace() const {
  if (custom_color_space_) return custom_color_space_;
  return GetGlobalColorSpace();
}

float UiWindow::GetScale() const { return GetUiScale(); }

float UiWindow::GetUiScale() {
  EnsureGlobalColorSpaceInitialized();
  std::scoped_lock lock(GetGlobalColorSpaceMutex());
  return GetGlobalScaleRef();
}

void UiWindow::OnGlobalColorSpaceChanged(sk_sp<SkColorSpace> new_color_space) {
  OnGlobalEnvironmentChanged(new_color_space, GetUiScale());
}

void UiWindow::OnGlobalEnvironmentChanged(sk_sp<SkColorSpace> new_color_space,
                                          float new_scale) {
  std::scoped_lock lock(GetGlobalColorSpaceMutex());
  if (new_color_space) GetGlobalColorSpaceRef() = new_color_space;
  if (new_scale >= 0.5f) GetGlobalScaleRef() = new_scale;
  for (UiWindow* window : GetOpenUiWindows()) {
    std::scoped_lock window_lock(window->window_mutex_);
    if (!window->custom_color_space_) {
      window->skia_surface_ = nullptr;
    }
    if (!window->node_.expired()) {
      auto node = window->node_.lock();
      float scale = window->GetScale();
      if (!window->is_resizable_ && window->created_ && window->base_window_) {
        Layout layout = node->GetLayout();
        auto width = layout.GetWidth();
        auto height = layout.GetHeight();
        layout.Calculate(
            width.unit == YGUnitAuto || width.value <= 0 ? YGUndefined
                                                         : width.value,
            height.unit == YGUnitAuto || height.value <= 0 ? YGUndefined
                                                           : height.value);
        int new_width = static_cast<int>(
            std::round(layout.GetCalculatedWidthWithMargin() * scale));
        int new_height = static_cast<int>(
            std::round(layout.GetCalculatedHeightWithMargin() * scale));
        window->base_window_->SetSize(new_width, new_height);
      } else {
        float logical_width = (float)window->buffer_width_ / scale;
        float logical_height = (float)window->buffer_height_ / scale;
        Layout layout = node->GetLayout();
        layout.SetWidth(logical_width);
        layout.SetHeight(logical_height);
      }
    }
    window->InvalidateRender();
  }
}

void UiWindow::SetParent(std::shared_ptr<UiWindow> parent_window) {
  std::scoped_lock lock(window_mutex_);
  parent_ui_window_ = parent_window;
}

void UiWindow::SetParent(std::shared_ptr<Node> parent_window_node) {
  if (!parent_window_node) return;
  SetParent(parent_window_node->Get<UiWindow>());
}

std::shared_ptr<window::Window> UiWindow::GetBaseWindow() const {
  std::scoped_lock lock(window_mutex_);
  return base_window_;
}

void UiWindow::SetNode(std::weak_ptr<Node> node) {
  std::scoped_lock lock(window_mutex_);
  node_ = node;
  if (node_.expired()) return;
  auto strong_node = node_.lock();
  strong_node->OnInvalidate([this](const std::optional<Rectangle>& dirty_area) {
    InvalidateRender(dirty_area);
  });
  InvalidateRender();
}

void UiWindow::SetBackgroundColor(uint32 background_color) {
  std::scoped_lock lock(window_mutex_);
  if (background_color_ == background_color) return;

  background_color_ = background_color;
  InvalidateRender();
}

void UiWindow::OnClose(std::function<void()> on_close_handler) {
  on_close_functions_.push_back(on_close_handler);
}

void UiWindow::OnResize(std::function<void()> on_resize_handler) {
  on_resize_functions_.push_back(on_resize_handler);
}

void UiWindow::SetTitle(std::string_view title) {
  if (title_ == title) return;
  title_ = title;

  if (created_ && base_window_) base_window_->SetTitle(title);
  for (auto& handler : on_title_changed_functions_) handler(title_);
}

std::string_view UiWindow::GetTitle() const { return title_; }

void UiWindow::OnTitleChanged(
    std::function<void(std::string_view)> on_title_changed) {
  on_title_changed_functions_.push_back(std::move(on_title_changed));
}

void UiWindow::SetIsResizable(bool is_resizable) {
  if (created_) return;
  is_resizable_ = is_resizable;
}

bool UiWindow::IsResizable() const { return is_resizable_; }

Size UiWindow::GetSystemButtonSize() const {
  std::scoped_lock lock(window_mutex_);
  if (!base_window_) {
    return Size{
        .width = is_resizable_ ? kTitleBarRightPaddingWithResizableButtons
                               : kTitleBarRightPaddingWithNonResizableButtons,
        .height = kDefaultSystemButtonHeight};
  }
  int physical_w = 0;
  int physical_h = 0;
  base_window_->GetSystemButtonSize(physical_w, physical_h);
  float scale = GetScale();
  return Size{.width = static_cast<float>(physical_w) / scale,
              .height = static_cast<float>(physical_h) / scale};
}

void UiWindow::SetFitContent(bool width, bool height) {
  if (created_) return;
  fit_content_width_ = width;
  fit_content_height_ = height;
}

bool UiWindow::FitsContentWidth() const { return fit_content_width_; }

bool UiWindow::FitsContentHeight() const { return fit_content_height_; }

void UiWindow::OnFocusChanged(std::function<void()> on_focus_changed) {
  (void)NotifyOnFocusChanged(on_focus_changed);
}

uint64 UiWindow::NotifyOnFocusChanged(std::function<void()> on_focus_changed) {
  std::scoped_lock lock(window_mutex_);
  uint64 id = next_focus_changed_handler_id_++;
  on_focus_changed_functions_[id] = on_focus_changed;
  return id;
}

void UiWindow::StopNotifyingOnFocusChanged(uint64 handler_id) {
  std::scoped_lock lock(window_mutex_);
  on_focus_changed_functions_.erase(handler_id);
}

bool UiWindow::IsFocused() const {
  if (!base_window_) return false;
  return base_window_->IsFocused();
}

void UiWindow::StartDragging() {
  pressed_mouse_buttons_ = 0;
  mouse_captured_node_.reset();
  if (base_window_) base_window_->StartDragging();
}

void UiWindow::Focus() {
  if (base_window_) base_window_->Focus();
}

void UiWindow::Close() {
  std::scoped_lock lock(window_mutex_);
  if (base_window_) {
    base_window_.reset();
    WindowClosed();
  }
}

void UiWindow::SetFocusedNode(std::shared_ptr<Node> node) {
  std::scoped_lock lock(window_mutex_);
  auto old_focused = focused_node_.lock();
  if (old_focused == node) return;

  focused_node_ = node;
  InvalidateRender();
}

std::shared_ptr<Node> UiWindow::GetFocusedNode() const {
  auto node = focused_node_.lock();
  if (!node) return nullptr;

  auto root = node_.lock();
  if (!root) {
    const_cast<UiWindow*>(this)->focused_node_.reset();
    return nullptr;
  }

  auto current = node;
  bool in_window = false;
  while (current) {
    if (current == root) {
      in_window = true;
      break;
    }
    current = current->GetParent().lock();
  }

  if (!in_window) {
    const_cast<UiWindow*>(this)->focused_node_.reset();
    return nullptr;
  }

  return node;
}

void UiWindow::KeyPressed(const window::KeyboardKeyEvent& event) {
  std::scoped_lock lock(window_mutex_);
  if (auto node = focused_node_.lock()) {
    if (auto focusable = node->Get<Focusable>()) focusable->KeyDown(event);
  }
}

void UiWindow::KeyReleased(const window::KeyboardKeyEvent& event) {
  std::scoped_lock lock(window_mutex_);
  if (auto node = focused_node_.lock()) {
    if (auto focusable = node->Get<Focusable>()) focusable->KeyUp(event);
  }
}

void UiWindow::WindowClosed() {
  std::scoped_lock lock(window_mutex_);
  base_window_.reset();
  auto handlers = std::move(on_close_functions_);
  for (auto& handler : handlers) handler();
}

void UiWindow::WindowResized() {
  std::scoped_lock lock(window_mutex_);
  if (node_.expired()) return;

  auto node = node_.lock();

  if (base_window_) {
    buffer_width_ = base_window_->GetWidth();
    buffer_height_ = base_window_->GetHeight();
  }

  float scale = GetScale();
  float logical_width = (float)buffer_width_ / scale;
  float logical_height = (float)buffer_height_ / scale;

  last_logical_width_ = logical_width;
  last_logical_height_ = logical_height;

  Layout layout = node->GetLayout();
  layout.SetWidth(logical_width);
  layout.SetHeight(logical_height);
  skia_surface_.reset();

  for (auto& handler : on_resize_functions_) handler();
  InvalidateRender();
}

void UiWindow::WindowFocusChanged() {
  std::vector<std::function<void()>> handlers;
  {
    std::scoped_lock lock(window_mutex_);
    if (!IsFocused()) {
      pressed_mouse_buttons_ = 0;
      mouse_captured_node_.reset();
      if (base_window_ && base_window_->IsMouseCaptive())
        base_window_->SetCaptureMouse(false);
      if (auto node = GetFocusedNode()) {
        if (auto focusable = node->Get<Focusable>()) focusable->Unfocus();
      }
    }
    for (auto& [id, handler] : on_focus_changed_functions_)
      handlers.push_back(handler);
  }
  for (auto& handler : handlers) handler();
}

void UiWindow::SetCaptureMouse(bool capture) {
  std::scoped_lock lock(window_mutex_);
  if (base_window_) {
    base_window_->SetCaptureMouse(capture);
  }
}

bool UiWindow::IsMouseCaptive() const {
  std::scoped_lock lock(window_mutex_);
  if (base_window_) {
    return base_window_->IsMouseCaptive();
  }
  return false;
}

void UiWindow::MouseMoved(const window::MouseMoveEvent& event) {
  std::scoped_lock lock(window_mutex_);
  if (auto captured = mouse_captured_node_.lock()) {
    captured->MouseMoved(event);
  } else if (auto root = node_.lock()) {
    root->MouseMoved(event);
  }
}

void UiWindow::MouseClicked(const window::MouseClickEvent& event) {
  std::scoped_lock lock(window_mutex_);

  float scale = GetScale();
  Point point{.x = (float)event.x / scale, .y = (float)event.y / scale};
  last_mouse_position_ = point;
  MouseButton button = event.button;
  uint8 button_mask = static_cast<uint8>(1u << static_cast<uint8>(button));
  bool was_any_button_pressed = pressed_mouse_buttons_ != 0;

  if (event.was_pressed_down) {
    pressed_mouse_buttons_ |= button_mask;
    if (auto captured = mouse_captured_node_.lock();
        captured && was_any_button_pressed) {
      Point local_point = ScreenPointToNodePoint(captured, point);
      captured->MouseButtonDown(local_point, button);
      return;
    }

    Focus();
    std::shared_ptr<Node> focusable_node = nullptr;
    std::shared_ptr<Node> clicked_node = nullptr;
    GetNodesAt(point, [&focusable_node, &clicked_node](
                          Node& node, const Point& point_in_node) {
      if (!clicked_node && node.DoesHandleMouseClickEvents())
        clicked_node = node.ToSharedPtr();
      if (!focusable_node && node.Get<Focusable>())
        focusable_node = node.ToSharedPtr();
    });
    if (focusable_node) {
      focusable_node->Get<Focusable>()->Focus();
    } else {
      if (auto current_focused = GetFocusedNode()) {
        if (auto focusable = current_focused->Get<Focusable>())
          focusable->Unfocus();
      }
    }

    if (clicked_node) mouse_captured_node_ = clicked_node;

    HandleMouseEvent(point,
                     [this, button](Node& node, const Point& point_in_node) {
                       node.MouseButtonDown(point_in_node, button);
                     });
  } else {
    pressed_mouse_buttons_ &= ~button_mask;
    if (auto captured = mouse_captured_node_.lock()) {
      Point local_point = ScreenPointToNodePoint(captured, point);
      captured->MouseButtonUp(local_point, button);
      if (pressed_mouse_buttons_ == 0) {
        mouse_captured_node_.reset();
        HandleMouseEvent(point, [](Node&, const Point&) {});
      }
    } else if (pressed_mouse_buttons_ == 0) {
      HandleMouseEvent(point, [](Node&, const Point&) {});
    }
  }
}

void UiWindow::MouseLeft() {
  std::scoped_lock lock(window_mutex_);

  pressed_mouse_buttons_ = 0;
  mouse_captured_node_.reset();
  for (std::weak_ptr<Node> node : nodes_to_notify_when_mouse_leaves_) {
    if (!node.expired()) node.lock()->MouseLeave();
  }
  nodes_to_notify_when_mouse_leaves_.clear();
}

void UiWindow::MouseHovered(const window::MouseHoverEvent& event) {
  std::scoped_lock lock(window_mutex_);
  float scale = GetScale();
  Point point{.x = (float)event.x / scale, .y = (float)event.y / scale};
  last_mouse_position_ = point;

  std::optional<window::Cursor> active_cursor;

  if (auto captured = mouse_captured_node_.lock()) {
    Point local_point = ScreenPointToNodePoint(captured, point);
    captured->MouseHover(local_point);
    auto opt_cursor = captured->GetCursor();
    if (opt_cursor) active_cursor = *opt_cursor;
  } else {
    HandleMouseEvent(point,
                     [&active_cursor](Node& node, const Point& point_in_node) {
                       node.MouseHover(point_in_node);
                       auto opt_cursor = node.GetCursor();
                       if (opt_cursor && !active_cursor.has_value()) {
                         active_cursor = *opt_cursor;
                       }
                     });
  }

  window::Cursor preferred_cursor =
      active_cursor.value_or(window::Cursor::Pointer);
  if (base_window_ && (!last_cursor_ || *last_cursor_ != preferred_cursor)) {
    last_cursor_ = preferred_cursor;
    base_window_->SetCursor(preferred_cursor);
  }
}

void UiWindow::MouseScrolled(const window::MouseScrollEvent& event) {
  std::scoped_lock lock(window_mutex_);
  Point remaining_delta{.x = event.delta_x * kScrollPixelsPerWheelStep,
                        .y = event.delta * kScrollPixelsPerWheelStep};
  if (remaining_delta.x == 0.0f && remaining_delta.y == 0.0f) return;

  GetNodesAt(last_mouse_position_,
             [&remaining_delta](Node& node, const Point& point_in_node) {
               if (remaining_delta.x == 0.0f && remaining_delta.y == 0.0f)
                 return;
               if (!node.HasOnMouseScroll()) return;
               Point consumed =
                   node.MouseScroll(point_in_node, remaining_delta);
               remaining_delta -= consumed;
             });
}

void UiWindow::PopulateDebuggingNodes(std::shared_ptr<Node> node) {
  if (!node) return;
  uint64 node_id = reinterpret_cast<uint64>(node.get());
  debugging_nodes_by_id_[node_id] = node;
  for (auto& child : node->GetChildren()) PopulateDebuggingNodes(child);
}

window::DebugUiHierarchy UiWindow::GetUiHierarchy() {
  std::scoped_lock lock(window_mutex_);
  window::DebugUiHierarchy hierarchy;
  if (node_.expired()) return hierarchy;
  auto node = node_.lock();

  debugging_nodes_by_id_.clear();
  PopulateDebuggingNodes(node);

  hierarchy.json = node->ToJson(title_);
  return hierarchy;
}

window::TweakUiResponse UiWindow::TweakUi(
    const window::TweakUiRequest& request) {
  std::scoped_lock lock(window_mutex_);
  window::TweakUiResponse response;

  for (auto& tweak : request.property_tweaks) {
    auto itr = debugging_nodes_by_id_.find(tweak.node_id);
    if (itr != debugging_nodes_by_id_.end() && !itr->second.expired()) {
      if (auto strong_node = itr->second.lock()) {
        strong_node->TweakProperty(tweak.property_name, tweak.property_value);
      }
    }
  }

  for (auto& create : request.create_nodes) {
    auto itr = debugging_nodes_by_id_.find(create.parent_node_id);
    if (itr != debugging_nodes_by_id_.end() && !itr->second.expired()) {
      if (auto parent_node = itr->second.lock()) {
        auto new_node = Node::Empty();
        parent_node->AddChild(new_node);
        uint64 real_id = reinterpret_cast<uint64>(new_node.get());
        debugging_nodes_by_id_[real_id] = new_node;

        window::CreateNodeResponse create_resp;
        create_resp.temp_id = create.temp_id;
        create_resp.real_id = real_id;
        response.create_node_responses.push_back(create_resp);
      }
    }
  }

  for (auto& del : request.delete_nodes) {
    auto itr = debugging_nodes_by_id_.find(del.node_id);
    if (itr != debugging_nodes_by_id_.end() && !itr->second.expired()) {
      if (auto node_to_delete = itr->second.lock()) {
        if (auto parent = node_to_delete->GetParent().lock()) {
          parent->RemoveChild(node_to_delete);
        }
        debugging_nodes_by_id_.erase(itr);
      }
    }
  }

  for (auto& reparent : request.reparent_nodes) {
    auto node_itr = debugging_nodes_by_id_.find(reparent.node_id);
    auto parent_itr = debugging_nodes_by_id_.find(reparent.new_parent_node_id);
    if (node_itr != debugging_nodes_by_id_.end() &&
        !node_itr->second.expired() &&
        parent_itr != debugging_nodes_by_id_.end() &&
        !parent_itr->second.expired()) {
      if (auto node = node_itr->second.lock()) {
        if (auto new_parent = parent_itr->second.lock()) {
          if (auto old_parent = node->GetParent().lock())
            old_parent->RemoveChild(node);
          new_parent->AddChild(node);
        }
      }
    }
  }

  InvalidateRender();
  return response;
}

void UiWindow::Draw() {
  if (!created_) Create();

  std::scoped_lock lock(window_mutex_);

  if (!invalidated_) return;
  invalidated_ = false;
  if (base_window_) {
    if (full_repaint_needed_ || !dirty_rect_.has_value()) {
      full_repaint_needed_ = false;
      dirty_rect_ = std::nullopt;
      base_window_->Present();
    } else {
      float scale = GetScale();
      int min_x = std::max(
          0, static_cast<int>(std::floor(dirty_rect_->origin.x * scale)));
      int min_y = std::max(
          0, static_cast<int>(std::floor(dirty_rect_->origin.y * scale)));
      int max_x = std::min(
          buffer_width_,
          static_cast<int>(std::ceil(
              (dirty_rect_->origin.x + dirty_rect_->size.width) * scale)));
      int max_y = std::min(
          buffer_height_,
          static_cast<int>(std::ceil(
              (dirty_rect_->origin.y + dirty_rect_->size.height) * scale)));

      dirty_rect_ = std::nullopt;
      full_repaint_needed_ = false;
      if (max_x > min_x && max_y > min_y)
        base_window_->Present(window::Rectangle(min_x, min_y, max_x, max_y));
    }
  }
}

void UiWindow::GetNodesAt(
    const Point& point,
    const std::function<void(Node& node, const Point& point_in_node)>&
        on_hit_node) {
  if (node_.expired()) return;
  auto node = node_.lock();
  float scale = GetScale();
  float logical_width = (float)buffer_width_ / scale;
  float logical_height = (float)buffer_height_ / scale;
  node->GetLayout().CalculateIfDirty(logical_width, logical_height);
  (void)node->GetNodesAt(point, on_hit_node);
}

void UiWindow::InvalidateRender(const std::optional<Rectangle>& dirty_area) {
  std::scoped_lock lock(window_mutex_);
  if (!dirty_area.has_value()) {
    full_repaint_needed_ = true;
    dirty_rect_ = std::nullopt;
  } else if (!full_repaint_needed_) {
    if (dirty_rect_.has_value()) {
      dirty_rect_ = dirty_rect_->Union(*dirty_area);
    } else {
      dirty_rect_ = *dirty_area;
    }
  }

  if (invalidated_ && !is_drawing_) return;

  invalidated_ = true;

  auto self = shared_from_this();
  DeferAfterEvents([self]() { self->Draw(); });
}

void UiWindow::WindowDraw(const window::WindowDrawBuffer& buffer,
                          window::Rectangle& invalidated_area) {
  std::scoped_lock lock(window_mutex_);
  if (node_.expired()) return;
  auto node = node_.lock();
  if (!skia_surface_ || buffer_width_ != buffer.width ||
      buffer_height_ != buffer.height || buffer.pixel_data != pixel_data_) {
    buffer_width_ = buffer.width;
    buffer_height_ = buffer.height;
    pixel_data_ = buffer.pixel_data;

    auto image_info = SkImageInfo::Make(
        buffer_width_, buffer_height_, SkColorType::kBGRA_8888_SkColorType,
        SkAlphaType::kOpaque_SkAlphaType, GetColorSpace());

    skia_surface_ =
        SkSurfaces::WrapPixels(image_info, pixel_data_, buffer_width_ * 4);
    if (!skia_surface_) {
      std::cout << "[UI Window Error] Skia surface wrapping failed! Buffer: "
                << pixel_data_ << " Size: " << buffer_width_ << "x"
                << buffer_height_ << std::endl;
      return;
    }
  }

  float scale = GetScale();
  float logical_width = (float)buffer_width_ / scale;
  float logical_height = (float)buffer_height_ / scale;

  // Set up the DrawContext to draw into back buffer.
  DrawContext draw_context;
  draw_context.buffer = static_cast<uint32*>(pixel_data_);
  draw_context.skia_canvas = skia_surface_->getCanvas();
  draw_context.buffer_width = buffer_width_;
  draw_context.buffer_height = buffer_height_;

  draw_context.area = {
      .origin = {.x = 0.0f, .y = 0.0f},
      .size = {.width = logical_width, .height = logical_height}};

  float clip_min_x = (float)invalidated_area.min_x / scale;
  float clip_min_y = (float)invalidated_area.min_y / scale;
  float clip_max_x = (float)invalidated_area.max_x / scale;
  float clip_max_y = (float)invalidated_area.max_y / scale;

  draw_context.clipping_bounds = {
      .origin = {.x = clip_min_x, .y = clip_min_y},
      .size = {.width = std::max(0.0f, clip_max_x - clip_min_x),
               .height = std::max(0.0f, clip_max_y - clip_min_y)}};

  if (background_color_)
    FillRectangle(invalidated_area.min_x, invalidated_area.min_y,
                  invalidated_area.max_x, invalidated_area.max_y,
                  background_color_, draw_context.buffer,
                  draw_context.buffer_width, draw_context.buffer_height);

  Layout layout = node->GetLayout();
  if (last_logical_width_ != logical_width ||
      last_logical_height_ != logical_height) {
    last_logical_width_ = logical_width;
    last_logical_height_ = logical_height;
    layout.SetWidth(logical_width);
    layout.SetHeight(logical_height);
  }
  layout.CalculateIfDirty(logical_width, logical_height);

  draw_context.skia_canvas->save();
  draw_context.skia_canvas->clipRect(
      SkRect::MakeLTRB(invalidated_area.min_x, invalidated_area.min_y,
                       invalidated_area.max_x, invalidated_area.max_y));
  draw_context.skia_canvas->scale(scale, scale);

  is_drawing_ = true;
  node->Draw(draw_context);
  is_drawing_ = false;
  draw_context.skia_canvas->restore();
}

void UiWindow::Create() {
  std::scoped_lock lock(window_mutex_);
  if (created_) return;

  if (node_.expired()) return;
  auto strong_node = node_.lock();

  std::shared_ptr<window::Window> parent_base_window;
  if (auto parent_ui = parent_ui_window_.lock())
    parent_base_window = parent_ui->GetBaseWindow();

  window::Window::CreationOptions options{.parent_window = parent_base_window,
                                          .title = title_,
                                          .is_resizable = is_resizable_,
                                          .is_double_buffered = true};

  Layout layout = strong_node->GetLayout();

  // Measure how big the content is.

  // If a dimension is 'auto', it fills to take the entire size passed in, but
  // if YGUndefined is passed in, it'll fill to wrap the content.
  auto width = layout.GetWidth();
  auto height = layout.GetHeight();
  layout.Calculate(
      width.unit == YGUnitAuto || width.value <= 0 ? YGUndefined : width.value,
      height.unit == YGUnitAuto || height.value <= 0 ? YGUndefined
                                                     : height.value);
  float scale = GetScale();
  int calculated_width = static_cast<int>(
      std::round(layout.GetCalculatedWidthWithMargin() * scale));
  int calculated_height = static_cast<int>(
      std::round(layout.GetCalculatedHeightWithMargin() * scale));

  if (is_resizable_) {
    if ((width.unit == YGUnitAuto || width.value <= 0) && !fit_content_width_)
      calculated_width = 0;
    if ((height.unit == YGUnitAuto || height.value <= 0) &&
        !fit_content_height_)
      calculated_height = 0;
  }

  options.prefered_width = calculated_width;
  options.prefered_height = calculated_height;

  base_window_ = window::Window::CreateWindow(options);
  if (base_window_) {
    auto this_as_window = shared_from_this();
    auto this_as_delegate =
        std::static_pointer_cast<window::WindowDelegate>(this_as_window);

    base_window_->SetDelegate(this_as_delegate);
    buffer_width_ = base_window_->GetWidth();
    buffer_height_ = base_window_->GetHeight();
  } else {
    buffer_width_ = 0;
    buffer_height_ = 0;
  }

  for (auto& handler : on_resize_functions_) handler();

  float logical_width = (float)buffer_width_ / scale;
  float logical_height = (float)buffer_height_ / scale;
  layout.SetWidth(logical_width);
  layout.SetHeight(logical_height);
  layout.Calculate(logical_width, logical_height);
  InvalidateRender();
  created_ = true;
}

void UiWindow::HandleMouseEvent(
    const Point& point,
    const std::function<void(Node& node, const Point& point_in_node)>&
        on_each_node) {
  std::vector<std::weak_ptr<Node>> new_nodes_to_notify_when_mouse_leaves;
  std::vector<std::pair<std::shared_ptr<Node>, Point>> hit_nodes;

  GetNodesAt(point, [&new_nodes_to_notify_when_mouse_leaves, &hit_nodes,
                     this](Node& node, const Point& point_in_node) {
    hit_nodes.push_back({node.ToSharedPtr(), point_in_node});
    if (node.DoesHandleMouseLeaveEvents())
      new_nodes_to_notify_when_mouse_leaves.push_back(node.ToSharedPtr());
  });

  for (const auto& [node, point_in_node] : hit_nodes)
    on_each_node(*node, point_in_node);

  for (const std::weak_ptr<Node>& old_node_weak :
       nodes_to_notify_when_mouse_leaves_) {
    auto old_node = old_node_weak.lock();
    if (!old_node) continue;
    bool still_hovered = false;
    for (const auto& new_node_weak : new_nodes_to_notify_when_mouse_leaves) {
      if (new_node_weak.lock() == old_node) {
        still_hovered = true;
        break;
      }
    }
    if (!still_hovered) old_node->MouseLeave();
  }
  nodes_to_notify_when_mouse_leaves_ =
      std::move(new_nodes_to_notify_when_mouse_leaves);
}

}  // namespace components
}  // namespace ui
}  // namespace perception
