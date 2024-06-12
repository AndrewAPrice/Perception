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

#include "perception/ui/ui_window.h"

#include "include/core/SkColorSpace.h"
#include "include/core/SkGraphics.h"
#include "include/core/SkSurface.h"
#include "perception/draw.h"
#include "perception/scheduler.h"
#include "perception/ui/draw_context.h"
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

namespace perception {
namespace ui {

UiWindow::UiWindow(std::string_view title, bool dialog)
    : title_(title),
      created_(false),
      is_dialog_(dialog),
      background_color_(kBackgroundWindowColor),
      invalidated_(false),
      buffer_width_(0),
      buffer_height_(0) {
  SkGraphics::Init();  // See if this isn't needed.
  SetWidthAuto();
  SetHeightAuto();
  SetPadding(YGEdgeAll, kMarginAroundWidgets);
}

UiWindow::~UiWindow() {}

UiWindow* UiWindow::SetBackgroundColor(uint32 background_color) {
  std::scoped_lock lock(window_mutex_);
  if (background_color_ == background_color) return this;

  background_color_ = background_color;
  InvalidateRender();
  return this;
}

UiWindow* UiWindow::OnClose(std::function<void()> on_close_handler) {
  std::scoped_lock lock(window_mutex_);
  on_close_handler_ = on_close_handler;
  return this;
}

UiWindow* UiWindow::OnResize(
    std::function<void(float, float)> on_resize_handler) {
  std::scoped_lock lock(window_mutex_);
  on_resize_handler_ = on_resize_handler;
  return this;
}

void UiWindow::WindowClosed() {
  std::scoped_lock lock(window_mutex_);
  if (on_close_handler_) on_close_handler_();
}

void UiWindow::WindowResized() {
  std::scoped_lock lock(window_mutex_);
  buffer_width_ = base_window_->GetWidth();
  buffer_height_ = base_window_->GetHeight();
  SetWidth((float)buffer_width_);
  SetHeight((float)buffer_height_);
  skia_surface_.reset();
  if (on_resize_handler_)
    on_resize_handler_((float)buffer_width_, (float)buffer_height_);
  InvalidateRender();
}

void UiWindow::MouseClicked(const window::MouseClickEvent& event) {
  std::scoped_lock lock(window_mutex_);
  std::shared_ptr<Widget> widget;
  float x_in_selected_widget, y_in_selected_widget;
  float x = (float)event.x;
  float y = (float)event.y;

  (void)GetWidgetAt(x, y, widget, x_in_selected_widget, y_in_selected_widget);

  SwitchToMouseOverWidget(widget);

  // Tell the widget the mouse has moved.
  if (widget) {
    if (event.was_pressed_down) {
      widget->OnMouseButtonDown(x_in_selected_widget, y_in_selected_widget,
                                event.button);
    } else {
      widget->OnMouseButtonUp(x_in_selected_widget, y_in_selected_widget,
                              event.button);
    }
  }
}

void UiWindow::MouseLeft() {
  std::scoped_lock lock(window_mutex_);
  if (auto widget = widget_mouse_is_over_.lock()) {
    widget->OnMouseLeave();
  }
  widget_mouse_is_over_.reset();
}

void UiWindow::MouseHovered(const window::MouseHoverEvent& event) {
  std::scoped_lock lock(window_mutex_);
  std::shared_ptr<Widget> widget;
  float x_in_selected_widget, y_in_selected_widget;
  float x = (float)event.x;
  float y = (float)event.y;

  (void)GetWidgetAt(x, y, widget, x_in_selected_widget, y_in_selected_widget);

  SwitchToMouseOverWidget(widget);

  // Tell the widget the mouse has moved.
  if (widget) {
    widget->OnMouseMove(x_in_selected_widget, y_in_selected_widget);
  }
}

void UiWindow::Draw() {
  std::cout << "Draw lock" << std::endl;
  std::scoped_lock lock(window_mutex_);

  std::cout << "Draw now have lock" << std::endl;
  if (!invalidated_ || !created_) {
    std::cout << "early return!" << std::endl;
    invalidated_ = false;
    return;
  }
  base_window_->Present();
  invalidated_ = false;
}

void Create() {}

bool UiWindow::GetWidgetAt(float x, float y, std::shared_ptr<Widget>& widget,
                           float& x_in_selected_widget,
                           float& y_in_selected_widget) {
  MaybeUpdateLayout();
  return Widget::GetWidgetAt(x, y, widget, x_in_selected_widget,
                             y_in_selected_widget);
}

void UiWindow::InvalidateRender() {
  if (invalidated_) {
    return;
  }

  DeferAfterEvents([this]() { Draw(); });

  invalidated_ = true;
}

void UiWindow::MaybeUpdateLayout() {
  if (YGNodeIsDirty(yoga_node_)) {
    YGNodeCalculateLayout(yoga_node_, (float)buffer_width_,
                          (float)buffer_height_, YGDirectionLTR);
  }
}

void UiWindow::SwitchToMouseOverWidget(std::shared_ptr<Widget> widget) {
  auto old_widget = widget_mouse_is_over_.lock();
  if (widget == old_widget) return;

  // The widget we are over has changed.
  if (old_widget) old_widget->OnMouseLeave();

  if (widget) {
    // We have a new widget.
    widget->OnMouseEnter();
    widget_mouse_is_over_ = widget;
  } else {
    // There is no new widget.
    widget_mouse_is_over_.reset();
  }
}

void UiWindow::WindowDraw(const window::WindowDrawBuffer& buffer,
                          window::Rectangle& invalidated_area) {
  std::cout << "WindowDraw" << std::endl;
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

  // Set up our DrawContext to draw into back buffer.
  DrawContext draw_context;
  draw_context.buffer = static_cast<uint32*>(pixel_data_);
  draw_context.skia_canvas = skia_surface_->getCanvas();
  draw_context.buffer_width = buffer_width_;
  draw_context.buffer_height = buffer_height_;
  draw_context.offset_x = 0.0f;
  draw_context.offset_y = 0.0f;

  if (background_color_) {
    FillRectangle(0, 0, buffer_width_, buffer_height_, background_color_,
                  draw_context.buffer, draw_context.buffer_width,
                  draw_context.buffer_height);
  }

  MaybeUpdateLayout();

  std::cout << "Invalidated area: " << invalidated_area.min_x << "," <<
    invalidated_area.min_y << "," << invalidated_area.max_x << "," <<
    invalidated_area.max_y << std::endl;

  Widget::Draw(draw_context);
}

UiWindow* UiWindow::Create() {
  std::scoped_lock lock(window_mutex_);
  if (created_) return this;

  window::Window::CreationOptions options{
      .title = title_,
      .is_resizable = !is_dialog_,
      .is_dialog = is_dialog_,
      .is_double_buffered = true
  };

  if (is_dialog_) {
    // Measure how big our dialog is.

    // If a dimension is 'auto', it fills to take the entire size passed in, but
    // if we pass in YGUndefined, it'll fill to wrap the content.
    auto width = GetWidth();
    auto height = GetHeight();
    YGNodeCalculateLayout(
        yoga_node_,
        width.unit == YGUnitAuto || width.value <= 0 ? YGUndefined
                                                     : width.value,
        height.unit == YGUnitAuto || height.value <= 0 ? YGUndefined
                                                       : height.value,
        YGDirectionLTR);
    options.prefered_width = GetCalculatedWidthWithMargin();
    options.prefered_height = GetCalculatedHeightWithMargin();
  }

  base_window_ = window::Window::CreateWindow(options);
  if (base_window_) {
    // Can't cast directly from Widget -> WindowDelegate;
    auto this_as_window = std::static_pointer_cast<UiWindow>(shared_from_this());
    auto this_as_delegate = std::static_pointer_cast<window::WindowDelegate>(this_as_window);
    base_window_->SetDelegate(this_as_delegate);
    buffer_width_ = base_window_->GetWidth();
    buffer_height_ = base_window_->GetHeight();
  } else {
    buffer_width_ = 0;
    buffer_height_ = 0;
  }
  std::cout << "Buffer width: " << buffer_width_
            << " - height: " << buffer_height_ << std::endl;

  if (on_resize_handler_)
    on_resize_handler_((float)buffer_width_, (float)buffer_height_);

  if (buffer_width_ != GetCalculatedWidthWithMargin() ||
      buffer_height_ != GetCalculatedHeightWithMargin()) {
    YGNodeCalculateLayout(yoga_node_, (float)buffer_width_,
                          (float)buffer_height_, YGDirectionLTR);
  }

  InvalidateRender();
  created_ = true;
  return this;
}

}  // namespace ui
}  // namespace perception
