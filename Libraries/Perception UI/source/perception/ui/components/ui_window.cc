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

#include "include/core/SkColorSpace.h"
#include "include/core/SkGraphics.h"
#include "include/core/SkSurface.h"
#include "perception/draw.h"
#include "perception/scheduler.h"
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

using ::perception::window::MouseButton;

namespace perception {
template class UniqueIdentifiableType<ui::components::UiWindow>;
namespace ui {
namespace components {

UiWindow::UiWindow()
    : created_(false),
      background_color_(kBackgroundWindowColor),
      invalidated_(false),
      buffer_width_(0),
      buffer_height_(0) {
  SkGraphics::Init();  // See if this isn't needed.
}

UiWindow::~UiWindow() {}

void UiWindow::SetNode(std::weak_ptr<Node> node) {
  std::scoped_lock lock(window_mutex_);
  node_ = node;
  if (node_.expired()) return;
  auto strong_node = node_.lock();
  strong_node->OnInvalidate(std::bind_front(&UiWindow::InvalidateRender, this));
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
}

void UiWindow::SetIsResizable(bool is_resizable) {
  if (created_) return;
  is_resizable_ = is_resizable;
}

bool UiWindow::IsResizable() const { return is_resizable_; }

void UiWindow::OnFocusChanged(std::function<void()> on_focus_changed) {
  on_focus_changed_functions_.push_back(on_focus_changed);
}

bool UiWindow::IsFocused() const {
  if (!base_window_) return false;
  return base_window_->IsFocused();
}

void UiWindow::StartDragging() {
  if (base_window_) base_window_->StartDragging();
}

void UiWindow::WindowClosed() {
  std::scoped_lock lock(window_mutex_);
  for (auto& handler : on_close_functions_) handler();
}

void UiWindow::WindowResized() {
  std::scoped_lock lock(window_mutex_);
  if (node_.expired()) return;

  auto node = node_.lock();

  if (base_window_) {
    buffer_width_ = base_window_->GetWidth();
    buffer_height_ = base_window_->GetHeight();
  }

  Layout layout = node->GetLayout();
  layout.SetWidth((float)buffer_width_);
  layout.SetHeight((float)buffer_height_);
  skia_surface_.reset();

  for (auto& handler : on_resize_functions_) handler();
  InvalidateRender();
}

void UiWindow::WindowFocusChanged() {
  for (auto& handler : on_focus_changed_functions_) handler();
}

void UiWindow::MouseClicked(const window::MouseClickEvent& event) {
  std::scoped_lock lock(window_mutex_);

  Point point{.x = (float)event.x, .y = (float)event.y};
  MouseButton button = event.button;

  if (event.was_pressed_down) {
    HandleMouseEvent(point,
                     [this, button](Node& node, const Point& point_in_node) {
                       node.MouseButtonDown(point_in_node, button);
                     });
  } else {
    HandleMouseEvent(point,
                     [this, button](Node& node, const Point& point_in_node) {
                       node.MouseButtonUp(point_in_node, button);
                     });
  }
}

void UiWindow::MouseLeft() {
  std::scoped_lock lock(window_mutex_);

  for (std::weak_ptr<Node> node : nodes_to_notify_when_mouse_leaves_) {
    if (!node.expired()) node.lock()->MouseLeave();
  }
  nodes_to_notify_when_mouse_leaves_.clear();
}

void UiWindow::MouseHovered(const window::MouseHoverEvent& event) {
  std::scoped_lock lock(window_mutex_);
  Point point{.x = (float)event.x, .y = (float)event.y};
  HandleMouseEvent(point, [this](Node& node, const Point& point_in_node) {
    node.MouseHover(point_in_node);
  });
}

void UiWindow::Draw() {
  if (!created_) Create();

  std::scoped_lock lock(window_mutex_);

  if (!invalidated_) return;
  if (base_window_) {
    base_window_->Present();
  }
  invalidated_ = false;
}

void UiWindow::GetNodesAt(
    const Point& point,
    const std::function<void(Node& node, const Point& point_in_node)>&
        on_hit_node) {
  if (node_.expired()) return;
  auto node = node_.lock();
  node->GetLayout().CalculateIfDirty(buffer_width_, buffer_height_);
  (void)node->GetNodesAt(point, on_hit_node);
}

void UiWindow::InvalidateRender() {
  if (invalidated_) {
    return;
  }

  invalidated_ = true;

  // auto weak_this = weak_from_this();
  auto self = shared_from_this();
  DeferAfterEvents([self]() {
    // if (!weak_this.expired()) weak_this.lock()->Draw();
    self->Draw();
  });
}

void UiWindow::WindowDraw(const window::WindowDrawBuffer& buffer,
                          window::Rectangle& invalidated_area) {
  if (node_.expired()) return;
  auto node = node_.lock();
  if (!skia_surface_ || buffer_width_ != buffer.width ||
      buffer_height_ != buffer.height || buffer.pixel_data != pixel_data_) {
    buffer_width_ = buffer.width;
    buffer_height_ = buffer.height;
    pixel_data_ = buffer.pixel_data;

    auto image_info = SkImageInfo::Make(
        buffer_width_, buffer_height_, SkColorType::kBGRA_8888_SkColorType,
        SkAlphaType::kOpaque_SkAlphaType, SkColorSpace::MakeSRGB());

    skia_surface_ =
        SkSurfaces::WrapPixels(image_info, pixel_data_, buffer_width_ * 4);
  }

  // Set up the DrawContext to draw into back buffer.
  DrawContext draw_context;
  draw_context.buffer = static_cast<uint32*>(pixel_data_);
  draw_context.skia_canvas = skia_surface_->getCanvas();
  draw_context.buffer_width = buffer_width_;
  draw_context.buffer_height = buffer_height_;

  float width = (float)buffer_width_;
  float height = (float)buffer_height_;
  draw_context.area = {.origin = {.x = 0.0f, .y = 0.0f},
                       .size = {.width = width, .height = height}};
  draw_context.clipping_bounds = draw_context.area;

  if (background_color_) {
    FillRectangle(0, 0, buffer_width_, buffer_height_, background_color_,
                  draw_context.buffer, draw_context.buffer_width,
                  draw_context.buffer_height);
  }

  node->GetLayout().CalculateIfDirty(buffer_width_, buffer_height_);
  node->Draw(draw_context);
}

void UiWindow::Create() {
  std::scoped_lock lock(window_mutex_);
  if (created_) return;

  if (node_.expired()) return;
  auto strong_node = node_.lock();

  window::Window::CreationOptions options{.title = title_,
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
  options.prefered_width = layout.GetCalculatedWidthWithMargin();
  options.prefered_height = layout.GetCalculatedHeightWithMargin();

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

  layout.Calculate((float)buffer_width_, (float)buffer_height_);
  InvalidateRender();
  created_ = true;
}

void UiWindow::HandleMouseEvent(
    const Point& point,
    const std::function<void(Node& node, const Point& point_in_node)>&
        on_each_node) {
  std::set<std::weak_ptr<Node>, NodeWeakPtrComparator>
      new_nodes_to_notify_when_mouse_leaves;

  GetNodesAt(point, [&new_nodes_to_notify_when_mouse_leaves, &on_each_node,
                     this](Node& node, const Point& point_in_node) {
    on_each_node(node, point_in_node);
    if (node.DoesHandleMouseLeaveEvents())
      new_nodes_to_notify_when_mouse_leaves.insert(node.ToSharedPtr());
  });

  for (std::weak_ptr<Node> node : nodes_to_notify_when_mouse_leaves_) {
    if (new_nodes_to_notify_when_mouse_leaves.count(node) == 0) {
      if (!node.expired()) node.lock()->MouseLeave();
    }
  }
  nodes_to_notify_when_mouse_leaves_ = new_nodes_to_notify_when_mouse_leaves;
}

}  // namespace components
}  // namespace ui
}  // namespace perception
