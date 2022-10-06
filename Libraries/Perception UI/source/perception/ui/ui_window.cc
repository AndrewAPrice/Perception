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

using ::permebuf::perception::Window;
using ::permebuf::perception::WindowManager;
using ::permebuf::perception::devices::GraphicsCommand;
using ::permebuf::perception::devices::GraphicsDriver;
using ::permebuf::perception::devices::KeyboardDriver;
using ::permebuf::perception::devices::KeyboardListener;
using ::permebuf::perception::devices::MouseButton;
using ::permebuf::perception::devices::MouseDriver;
using ::permebuf::perception::devices::MouseListener;

namespace perception {
namespace ui {

UiWindow::UiWindow(std::string_view title, bool dialog, int dialog_width,
                   int dialog_height)
    : title_(title),
      background_color_(kBackgroundWindowColor),
      texture_id_(0),
      frontbuffer_texture_id_(0),
      rebuild_texture_(true) {
  SkGraphics::Init();  // See if this isn't needed.

  Permebuf<WindowManager::CreateWindowRequest> create_window_request;
  // std::cout << title_ << "'s message id is: " <<
  //((Window::Server*)this)->GetProcessId() << ":" <<
  //((Window::Server*)this)->GetMessageId() << std::endl;
  create_window_request->SetWindow(*this);
  create_window_request->SetTitle(title);
  create_window_request->SetFillColor(0xFFFFFFFF);
  create_window_request->SetKeyboardListener(*this);
  create_window_request->SetMouseListener(*this);
  if (dialog) {
    create_window_request->SetIsDialog(true);
    create_window_request->SetDesiredDialogWidth(dialog_width);
    create_window_request->SetDesiredDialogHeight(dialog_height);
  }

  auto status_or_result =
      WindowManager::Get().CallCreateWindow(std::move(create_window_request));
  if (status_or_result) {
    buffer_width_ = status_or_result->GetWidth();
    buffer_height_ = status_or_result->GetHeight();
  } else {
    buffer_width_ = 0;
    buffer_height_ = 0;
  }
}

UiWindow::~UiWindow() {
  // std::cout << title_ << " - destroyed" << std::endl;
}

UiWindow* UiWindow::SetBackgroundColor(uint32 background_color) {
  if (background_color_ == background_color) return this;

  background_color_ = background_color;
  InvalidateRender();
  return this;
}

UiWindow* UiWindow::OnClose(std::function<void()> on_close_handler) {
  on_close_handler_ = on_close_handler;
  return this;
}

void UiWindow::HandleOnMouseMove(
    ProcessId, const MouseListener::OnMouseMoveMessage& message) {
  //  std::cout << title_ << " - x:" << message.GetDeltaX() <<
  //    " y:" << message.GetDeltaY() << std::endl;
}

void UiWindow::HandleOnMouseScroll(
    ProcessId, const MouseListener::OnMouseScrollMessage& message) {
  // std::cout << title_ << " mouse scrolled" << message.GetDelta() <<
  // std::endl;
}

void UiWindow::HandleOnMouseButton(
    ProcessId, const MouseListener::OnMouseButtonMessage& message) {
  // std::cout << title_ << " - button: " << (int)message.GetButton() <<
  //  " down: " << message.GetIsPressedDown() << std::endl;
}

void UiWindow::HandleOnMouseClick(
    ProcessId, const MouseListener::OnMouseClickMessage& message) {
  std::shared_ptr<Widget> widget;
  float x_in_selected_widget, y_in_selected_widget;
  float x = (float)message.GetX();
  float y = (float)message.GetY();

  (bool)GetWidgetAt(x, y, widget, x_in_selected_widget, y_in_selected_widget);

  SwitchToMouseOverWidget(widget);

  // Tell the widget the mouse has moved.
  if (widget) {
    if (message.GetWasPressedDown()) {
      widget->OnMouseButtonDown(x, y, message.GetButton());
    } else {
      widget->OnMouseButtonUp(x, y, message.GetButton());
    }
  }
}

void UiWindow::HandleOnMouseEnter(
    ProcessId, const MouseListener::OnMouseEnterMessage& message) {
  // std::cout << title_ << " - mouse entered." << std::endl;
}

void UiWindow::HandleOnMouseLeave(
    ProcessId, const MouseListener::OnMouseLeaveMessage& message) {
  if (auto widget = widget_mouse_is_over_.lock()) {
    widget->OnMouseLeave();
  }
  widget_mouse_is_over_.reset();
}

void UiWindow::HandleOnMouseHover(
    ProcessId, const MouseListener::OnMouseHoverMessage& message) {
  std::shared_ptr<Widget> widget;
  float x_in_selected_widget, y_in_selected_widget;
  float x = (float)message.GetX();
  float y = (float)message.GetY();

  (bool)GetWidgetAt(x, y, widget, x_in_selected_widget, y_in_selected_widget);

  SwitchToMouseOverWidget(widget);

  // Tell the widget the mouse has moved.
  if (widget) {
    widget->OnMouseMove(x, y);
  }
}

void UiWindow::HandleOnMouseTakenCaptive(
    ProcessId, const MouseListener::OnMouseTakenCaptiveMessage& message) {
  // std::cout << title_ << " - mouse taken captive." << std::endl;
}

void UiWindow::HandleOnMouseReleased(
    ProcessId, const MouseListener::OnMouseReleasedMessage& message) {
  // std::cout << title_ << " - mouse has been released." << std::endl;
}

void UiWindow::HandleOnKeyDown(
    ProcessId, const KeyboardListener::OnKeyDownMessage& message) {
  // std::cout << title_ << " - key " << (int)message.GetKey() << " was
  // pressed." << std::endl;
}

void UiWindow::HandleOnKeyUp(ProcessId,
                             const KeyboardListener::OnKeyUpMessage& message) {
  // std::cout << title_ << " - key " << (int)message.GetKey() << " was
  // released." << std::endl;
}

void UiWindow::HandleOnKeyboardTakenCaptive(
    ProcessId, const KeyboardListener::OnKeyboardTakenCaptiveMessage& message) {
  // std::cout << title_ << " - keyboard taken captive." << std::endl;
}

void UiWindow::HandleOnKeyboardReleased(
    ProcessId, const KeyboardListener::OnKeyboardReleasedMessage& message) {
  // std::cout << title_ << " - keyboard has been released." << std::endl;
}

void UiWindow::HandleSetSize(ProcessId, const Window::SetSizeMessage& message) {
  std::cout << "Window " << title_ << " resized to " << message.GetWidth()
            << "," << message.GetHeight() << std::endl;
  buffer_width_ = message.GetWidth();
  buffer_height_ = message.GetHeight();
  SetWidth((float)buffer_width_);
  SetHeight((float)buffer_height_);
  rebuild_texture_ = true;
  InvalidateRender();
}

void UiWindow::HandleClosed(ProcessId, const Window::ClosedMessage& message) {
  if (on_close_handler_) on_close_handler_();
}

void UiWindow::HandleGainedFocus(ProcessId,
                                 const Window::GainedFocusMessage& message) {
  // std::cout << title_ << " gained focus" << std::endl;
}

void UiWindow::HandleLostFocus(ProcessId,
                               const Window::LostFocusMessage& message) {
  // std::cout << title_ << " lost focus" << std::endl;
}

void UiWindow::Draw() {
  if (!invalidated_) return;

  if (rebuild_texture_) {
    // The window size has changed.

    ReleaseTextures();
    skia_surface_ = sk_sp<SkSurface>();

    if (buffer_width_ > 0 && buffer_height_ > 0) {
      // Create the back buffer we draw into.
      GraphicsDriver::CreateTextureRequest request;
      request.SetWidth(buffer_width_);
      request.SetHeight(buffer_height_);
      auto status_or_response =
          GraphicsDriver::Get().CallCreateTexture(request);
      if (status_or_response) {
        texture_id_ = status_or_response->GetTexture();
        texture_shared_memory_ = status_or_response->GetPixelBuffer();
      }

      // Create the front buffer.
      status_or_response = GraphicsDriver::Get().CallCreateTexture(request);
      if (status_or_response) {
        frontbuffer_texture_id_ = status_or_response->GetTexture();
        frontbuffer_shared_memory_ = status_or_response->GetPixelBuffer();
      }

      // Notify the window manager of our front buffer.
      WindowManager::SetWindowTextureMessage message;
      message.SetWindow(*this);
      message.SetTextureId(frontbuffer_texture_id_);
      WindowManager::Get().SendSetWindowTexture(message);
    }
    rebuild_texture_ = false;
  }

  if (buffer_width_ == 0 || buffer_height_ == 0 ||
      !texture_shared_memory_.Join() || !frontbuffer_shared_memory_.Join())
    return;

  if (!skia_surface_) {
    auto image_info = SkImageInfo::Make(
        buffer_width_, buffer_height_, SkColorType::kRGBA_8888_SkColorType,
        SkAlphaType::kOpaque_SkAlphaType, SkColorSpace::MakeSRGB());

    skia_surface_ = SkSurface::MakeRasterDirect(
        image_info, *texture_shared_memory_, buffer_width_);
  }

  // Set up our DrawContext to draw into back buffer.
  DrawContext draw_context;
  draw_context.buffer = static_cast<uint32*>(*texture_shared_memory_);
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

  if (YGNodeIsDirty(yoga_node_)) {
    YGNodeCalculateLayout(yoga_node_, (float)buffer_width_,
                          (float)buffer_height_, YGDirectionLTR);
  }

  Widget::Draw(draw_context);

  // Copy the backbuffer to the front buffer.
  memcpy(*frontbuffer_shared_memory_, *texture_shared_memory_,
         buffer_width_ * buffer_height_ * 4);

  // Tell the window manager the back buffer is ready to draw.
  WindowManager::InvalidateWindowMessage message;
  message.SetWindow(*this);
  message.SetLeft(0);
  message.SetTop(0);
  message.SetRight(static_cast<uint16>(buffer_width_));
  message.SetBottom(static_cast<uint16>(buffer_height_));
  WindowManager::Get().SendInvalidateWindow(message);

  invalidated_ = false;
}

void UiWindow::InvalidateRender() {
  if (invalidated_) {
    return;
  }

  Defer([this]() { Draw(); });

  invalidated_ = true;
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

void UiWindow::ReleaseTextures() {
  if (texture_id_ != 0) {
    // We have an old texture to release.
    GraphicsDriver::DestroyTextureMessage message;
    message.SetTexture(texture_id_);
    GraphicsDriver::Get().SendDestroyTexture(message);
    texture_id_ = 0;
    texture_shared_memory_ = SharedMemory();
  }

  if (frontbuffer_texture_id_ != 0) {
    // We have an old frontbuffer texture to release.
    GraphicsDriver::DestroyTextureMessage message;
    message.SetTexture(frontbuffer_texture_id_);
    GraphicsDriver::Get().SendDestroyTexture(message);
    frontbuffer_texture_id_ = 0;
    frontbuffer_shared_memory_ = SharedMemory();
  }
}

}  // namespace ui
}  // namespace perception
