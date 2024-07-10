// Copyright 2024 Google LLC
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
// #define PERCEPTION
#ifdef PERCEPTION

#include <memory>
#include <mutex>
#include <string_view>

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
#include "permebuf/Libraries/perception/devices/graphics_driver.permebuf.h"
#include "permebuf/Libraries/perception/devices/keyboard_driver.permebuf.h"
#include "permebuf/Libraries/perception/devices/keyboard_listener.permebuf.h"
#include "permebuf/Libraries/perception/devices/mouse_driver.permebuf.h"
#include "permebuf/Libraries/perception/devices/mouse_listener.permebuf.h"
#include "permebuf/Libraries/perception/window.permebuf.h"
#include "permebuf/Libraries/perception/window_manager.permebuf.h"

using ::permebuf::perception::Window;
using ::permebuf::perception::WindowManager;
using ::permebuf::perception::devices::GraphicsCommand;
using ::permebuf::perception::devices::GraphicsDriver;
using ::permebuf::perception::devices::KeyboardDriver;
using ::permebuf::perception::devices::KeyboardListener;
using ::permebuf::perception::devices::MouseDriver;
using ::permebuf::perception::devices::MouseListener;

namespace perception {
namespace window {
namespace {
// Implementation of perception::window::Window for the Perception operating
// system.

class PerceptionWindow : public Window,
                         public MouseListener::Server,
                         public KeyboardListener::Server,
                         public ::permebuf::perception::Window::Server {
 public:
  PerceptionWindow() : created_(true), rebuild_texture_(true) {}
  virtual ~PerceptionWindow() {}

  void SetInitialProperties(int width, int height, bool is_double_buffered) {
    width_ = width;
    height_ = height;
    is_double_buffered_ = is_double_buffered;
  }

  void SetDelegate(std::weak_ptr<WindowDelegate> delegate) override {
    delegate_ = delegate;
  }

  void SetTitle(std::string_view title) override {
    std::cout << "PerceptionWindow::SetTitle is not implemented." << std::endl;
  }

  int GetWidth() override { return width_; }

  int GetHeight() override { return height_; }

  void SetSize(int width, int height) override {
    if (width_ == width && height_ == height) return;
    std::cout << "PerceptionWindow::SetSize is not implemented." << std::endl;
  }

  float GetScale() override { return 1.0f; };

  bool IsFullScreen() override { return false; };

  void SetFullScreen(bool fullscreen) override {};

  bool IsMouseCaptive() override { return is_mouse_captive_; }

  void SetCaptureMouse(bool capture) override {
    if (is_mouse_captive_ == capture) return;
    std::cout << "PerceptionWindow::SetCaptureMouse is not implemented."
              << std::endl;
  }

  bool IsKeyboardCaptive() override { return is_keyboard_captive_; }

  void SetCaptureKeyboard(bool capture) override {
    if (is_keyboard_captive_ == capture) return;
    std::cout << "PerceptionWindow::SetCaptureKeyboard is not implemented."
              << std::endl;
  }

  bool IsFocused() override { return is_focused_; }

  void Focus() override {
    if (is_focused_) return;
    std::cout << "PerceptionWindow::Focus is not implemented." << std::endl;
  }

  void Present() override {
    WindowDrawBuffer buffer;
    buffer.width = width_;
    buffer.height = height_;
    if (rebuild_texture_) {
      RebuildTextures();
      buffer.has_preserved_contents_from_previous_draw = true;
      rebuild_texture_ = false;
    } else {
      buffer.has_preserved_contents_from_previous_draw = false;
    }

    if (width_ == 0 || height_ == 0 || !texture_shared_memory_.Join() ||
        (is_double_buffered_ && !frontbuffer_shared_memory_.Join())) {
      return;
    }

    buffer.pixel_data = *texture_shared_memory_;
    Rectangle invalidated_area{
        .min_x = 0, .min_y = 0, .max_x = width_, .max_y = height_};
    if (!delegate_.expired())
      delegate_.lock()->WindowDraw(buffer, invalidated_area);

    if (is_double_buffered_) {
      // TODO: Copy only what's in invalidated_rect.
      // Copy the backbuffer to the front buffer.
      memcpy(*frontbuffer_shared_memory_, *texture_shared_memory_,
             width_ * height_ * 4);
    }

    // Tell the window manager there is new data to draw.
    WindowManager::InvalidateWindowMessage message;
    message.SetWindow(*this);
    message.SetLeft(invalidated_area.min_x);
    message.SetTop(invalidated_area.min_y);
    message.SetRight(invalidated_area.max_x);
    message.SetBottom(invalidated_area.max_y);
    WindowManager::Get().SendInvalidateWindow(message);
  }

  /// MouseListener::Server
  void HandleOnMouseMove(
      ProcessId,
      const ::permebuf::perception::devices::MouseListener::OnMouseMoveMessage&
          message) override {
    if (!delegate_.expired()) {
      delegate_.lock()->MouseMoved(MouseMoveEvent{
          .delta_x = message.GetDeltaX(), .delta_y = message.GetDeltaY()});
    }
  }

  void HandleOnMouseScroll(
      ProcessId, const ::permebuf::perception::devices::MouseListener::
                     OnMouseScrollMessage& message) override {
    if (!delegate_.expired()) {
      delegate_.lock()->MouseScrolled(
          MouseScrollEvent{.delta = message.GetDelta()});
    }
  }

  void HandleOnMouseButton(
      ProcessId, const ::permebuf::perception::devices::MouseListener::
                     OnMouseButtonMessage& message) override {
    if (!delegate_.expired()) {
      delegate_.lock()->MouseButtonChanged(MouseButtonEvent{
          .button = static_cast<MouseButton>(message.GetButton()),
          .is_pressed_down = message.GetIsPressedDown()});
    }
  }

  void HandleOnMouseClick(
      ProcessId,
      const ::permebuf::perception::devices::MouseListener::OnMouseClickMessage&
          message) override {
    if (!delegate_.expired()) {
      delegate_.lock()->MouseClicked(MouseClickEvent{
          .button = static_cast<MouseButton>(message.GetButton()),
          .x = message.GetX(),
          .y = message.GetY(),
          .was_pressed_down = message.GetWasPressedDown()});
    }
  }

  void HandleOnMouseEnter(
      ProcessId,
      const ::permebuf::perception::devices::MouseListener::OnMouseEnterMessage&
          message) override {
    if (!delegate_.expired()) delegate_.lock()->MouseEntered();
  }

  void HandleOnMouseLeave(
      ProcessId,
      const ::permebuf::perception::devices::MouseListener::OnMouseLeaveMessage&
          message) override {
    if (!delegate_.expired()) delegate_.lock()->MouseLeft();
  }

  void HandleOnMouseHover(
      ProcessId,
      const ::permebuf::perception::devices::MouseListener::OnMouseHoverMessage&
          message) override {
    if (!delegate_.expired()) {
      delegate_.lock()->MouseHovered(
          MouseHoverEvent{.x = message.GetX(), .y = message.GetY()});
    }
  }

  void HandleOnMouseTakenCaptive(
      ProcessId, const ::permebuf::perception::devices::MouseListener::
                     OnMouseTakenCaptiveMessage& message) override {
    is_mouse_captive_ = true;
    if (!delegate_.expired()) delegate_.lock()->MouseCaptivityChanged();
  }

  void HandleOnMouseReleased(
      ProcessId, const ::permebuf::perception::devices::MouseListener::
                     OnMouseReleasedMessage& message) override {
    is_mouse_captive_ = false;
    if (!delegate_.expired()) delegate_.lock()->MouseCaptivityChanged();
  }

  // KeyboardListener::Server
  void HandleOnKeyDown(
      ProcessId,
      const ::permebuf::perception::devices::KeyboardListener::OnKeyDownMessage&
          message) override {
    if (!delegate_.expired())
      delegate_.lock()->KeyPressed(KeyboardKeyEvent{.key = message.GetKey()});
  }

  void HandleOnKeyUp(
      ProcessId,
      const ::permebuf::perception::devices::KeyboardListener::OnKeyUpMessage&
          message) override {
    if (!delegate_.expired())
      delegate_.lock()->KeyReleased(KeyboardKeyEvent{.key = message.GetKey()});
  }

  void HandleOnKeyboardTakenCaptive(
      ProcessId, const ::permebuf::perception::devices::KeyboardListener::
                     OnKeyboardTakenCaptiveMessage& message) override {
    is_keyboard_captive_ = true;
    if (!delegate_.expired()) delegate_.lock()->KeyboardCaptivityChanged();
  }

  void HandleOnKeyboardReleased(
      ProcessId, const ::permebuf::perception::devices::KeyboardListener::
                     OnKeyboardReleasedMessage& message) override {
    is_keyboard_captive_ = false;
    if (!delegate_.expired()) delegate_.lock()->KeyboardCaptivityChanged();
  }
  // Window::Server
  void HandleSetSize(
      ProcessId,
      const ::permebuf::perception::Window::SetSizeMessage& message) override {
    width_ = message.GetWidth();
    height_ = message.GetHeight();
    rebuild_texture_ = true;
    if (!delegate_.expired()) delegate_.lock()->WindowResized();
  }

  void HandleClosed(
      ProcessId,
      const ::permebuf::perception::Window::ClosedMessage& message) override {
    created_ = false;
    if (!delegate_.expired()) delegate_.lock()->WindowClosed();
  }

  void HandleGainedFocus(
      ProcessId,
      const ::permebuf::perception::Window::GainedFocusMessage& message)
      override {
    is_focused_ = true;
    if (!delegate_.expired()) delegate_.lock()->WindowFocusChanged();
  }

  void HandleLostFocus(ProcessId,
                       const ::permebuf::perception::Window::LostFocusMessage&
                           message) override {
    is_focused_ = false;
    if (!delegate_.expired()) delegate_.lock()->WindowFocusChanged();
  }

 private:
  std::weak_ptr<WindowDelegate> delegate_;
  std::mutex mutex_;
  int width_;
  int height_;
  bool is_double_buffered_;
  int texture_id_;
  int frontbuffer_texture_id_;
  SharedMemory texture_shared_memory_;
  SharedMemory frontbuffer_shared_memory_;
  bool created_;
  bool rebuild_texture_;
  bool is_keyboard_captive_;
  bool is_mouse_captive_;
  bool is_focused_;

  void ReleaseTextures() {
    if (texture_id_ != 0) {
      // There is an old texture to release.
      GraphicsDriver::DestroyTextureMessage message;
      message.SetTexture(texture_id_);
      GraphicsDriver::Get().SendDestroyTexture(message);
      texture_id_ = 0;
      texture_shared_memory_ = SharedMemory();
    }

    if (frontbuffer_texture_id_ != 0) {
      // There is an old texture to release.
      GraphicsDriver::DestroyTextureMessage message;
      message.SetTexture(frontbuffer_texture_id_);
      GraphicsDriver::Get().SendDestroyTexture(message);
      frontbuffer_texture_id_ = 0;
      frontbuffer_shared_memory_ = SharedMemory();
    }
  }

  void RebuildTextures() {
    ReleaseTextures();

    // Create the back buffer that's drawn into (and also the front
    // buffer if not double buffered.)
    GraphicsDriver::CreateTextureRequest request;
    request.SetWidth(width_);
    request.SetHeight(height_);
    auto status_or_response = GraphicsDriver::Get().CallCreateTexture(request);
    if (status_or_response) {
      texture_id_ = status_or_response->GetTexture();
      texture_shared_memory_ = status_or_response->GetPixelBuffer();
    }

    if (is_double_buffered_) {
      // Create the front buffer to present.
      status_or_response = GraphicsDriver::Get().CallCreateTexture(request);
      if (status_or_response) {
        frontbuffer_texture_id_ = status_or_response->GetTexture();
        frontbuffer_shared_memory_ = status_or_response->GetPixelBuffer();
      }
    }

    // Notify the window manager of the front buffer.
    WindowManager::SetWindowTextureMessage message;
    message.SetWindow(*this);
    message.SetTextureId(is_double_buffered_ ? frontbuffer_texture_id_
                                             : texture_id_);
    WindowManager::Get().SendSetWindowTexture(message);
  }
};

}  // namespace

std::shared_ptr<Window> Window::CreateWindow(
    const Window::CreationOptions& creation_options) {
  auto window = std::make_shared<PerceptionWindow>();
  Permebuf<WindowManager::CreateWindowRequest> create_window_request;
  create_window_request->SetWindow(*window);
  create_window_request->SetTitle(creation_options.title);
  create_window_request->SetKeyboardListener(*window);
  create_window_request->SetMouseListener(*window);
  create_window_request->SetIsDialog(creation_options.is_dialog);
  create_window_request->SetDesiredDialogWidth(creation_options.prefered_width);
  create_window_request->SetDesiredDialogHeight(
      creation_options.prefered_height);

  auto status_or_result =
      WindowManager::Get().CallCreateWindow(std::move(create_window_request));
  if (!status_or_result) {
    return nullptr;
  }

  window->SetInitialProperties((*status_or_result)->GetWidth(),
                               (*status_or_result)->GetHeight(),
                               creation_options.is_double_buffered);
  return std::static_pointer_cast<Window>(window);
}

}  // namespace window
}  // namespace perception
#endif
