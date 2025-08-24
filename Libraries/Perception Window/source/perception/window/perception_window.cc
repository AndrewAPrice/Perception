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

#include <iostream>
#include <memory>
#include <mutex>
#include <string_view>

#include "perception/devices/graphics_device.h"
#include "perception/devices/keyboard_device.h"
#include "perception/devices/keyboard_listener.h"
#include "perception/devices/mouse_device.h"
#include "perception/devices/mouse_listener.h"
#include "perception/services.h"
#include "perception/window/base_window.h"
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
#include "status.h"

namespace graphics = ::perception::devices::graphics;
using ::perception::GetService;
using ::perception::Status;
using ::perception::devices::GraphicsDevice;
using ::perception::devices::KeyboardEvent;
using ::perception::devices::KeyboardListener;
using ::perception::devices::MouseButtonEvent;
using ::perception::devices::MouseClickEvent;
using ::perception::devices::MouseListener;
using ::perception::devices::MousePositionEvent;
using ::perception::devices::RelativeMousePositionEvent;
using ::perception::window::BaseWindow;
using ::perception::window::CreateWindowRequest;
using ::perception::window::InvalidateWindowParameters;
using ::perception::window::SetWindowTextureParameters;
using ::perception::window::SetWindowTitleParameters;
using ::perception::window::WindowManager;

namespace perception {
namespace window {
namespace {
// Implementation of perception::window::Window for the Perception operating
// system.

class PerceptionWindow : public Window,
                         public BaseWindow::Server,
                         public MouseListener::Server,
                         public KeyboardListener::Server {
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

    if (width_ == 0 || height_ == 0 || !texture_shared_memory_->Join() ||
        (is_double_buffered_ && !frontbuffer_shared_memory_->Join())) {
      return;
    }

    buffer.pixel_data = **texture_shared_memory_;
    Rectangle invalidated_area{
        .min_x = 0, .min_y = 0, .max_x = width_, .max_y = height_};
    if (!delegate_.expired())
      delegate_.lock()->WindowDraw(buffer, invalidated_area);

    if (is_double_buffered_) {
      // TODO: Copy only what's in invalidated_rect.
      // Copy the backbuffer to the front buffer.
      memcpy(**frontbuffer_shared_memory_, **texture_shared_memory_,
             width_ * height_ * 4);
    }

    // Tell the window manager there is new data to draw.
    InvalidateWindowParameters message;
    message.window = *this;
    message.left = invalidated_area.min_x;
    message.top = invalidated_area.min_y;
    message.right = invalidated_area.max_x;
    message.bottom = invalidated_area.max_y;
    GetService<WindowManager>().InvalidateWindow(message, {});
  }

  /// MouseListener::Server
  virtual Status MouseMove(const RelativeMousePositionEvent& message) override {
    if (!delegate_.expired()) {
      delegate_.lock()->MouseMoved(MouseMoveEvent{.delta_x = message.delta_x,
                                                  .delta_y = message.delta_y});
    }
    return Status::OK;
  }

  virtual Status MouseScroll(
      const RelativeMousePositionEvent& message) override {
    if (!delegate_.expired()) {
      delegate_.lock()->MouseScrolled(
          MouseScrollEvent{.delta = message.delta_y});
    }
    return Status::OK;
  }

  virtual Status MouseButton(
      const ::perception::devices::MouseButtonEvent& message) override {
    if (!delegate_.expired()) {
      delegate_.lock()->MouseButtonChanged(MouseButtonEvent{
          .button = static_cast<enum MouseButton>(message.button),
          .is_pressed_down = message.is_pressed_down});
    }
    return Status::OK;
  }

  virtual Status MouseClick(
      const ::perception::devices::MouseClickEvent& message) override {
    if (!delegate_.expired()) {
      delegate_.lock()->MouseClicked(MouseClickEvent{
          .button = static_cast<enum MouseButton>(message.button.button),
          .x = static_cast<int>(message.position.x),
          .y = static_cast<int>(message.position.y),
          .was_pressed_down = message.button.is_pressed_down});
    }
    return Status::OK;
  }

  virtual Status MouseEnter() override {
    if (!delegate_.expired()) delegate_.lock()->MouseEntered();
    return Status::OK;
  }

  virtual Status MouseLeave() override {
    if (!delegate_.expired()) delegate_.lock()->MouseLeft();
    return Status::OK;
  }

  virtual Status MouseHover(const MousePositionEvent& message) override {
    if (!delegate_.expired()) {
      delegate_.lock()->MouseHovered(MouseHoverEvent{
          .x = static_cast<int>(message.x), .y = static_cast<int>(message.y)});
    }
    return Status::OK;
  }

  virtual Status MouseTakenCaptive() override {
    is_mouse_captive_ = true;
    if (!delegate_.expired()) delegate_.lock()->MouseCaptivityChanged();
    return Status::OK;
  }

  virtual Status MouseReleased() override {
    is_mouse_captive_ = false;
    if (!delegate_.expired()) delegate_.lock()->MouseCaptivityChanged();
    return Status::OK;
  }

  // KeyboardListener::Server
  virtual Status KeyDown(const KeyboardEvent& message) override {
    if (!delegate_.expired())
      delegate_.lock()->KeyPressed(KeyboardKeyEvent{.key = message.key});
    return Status::OK;
  }

  virtual Status KeyUp(const KeyboardEvent& message) override {
    if (!delegate_.expired())
      delegate_.lock()->KeyReleased(KeyboardKeyEvent{.key = message.key});
    return Status::OK;
  }

  virtual Status KeyboardTakenCaptive() override {
    is_keyboard_captive_ = true;
    if (!delegate_.expired()) delegate_.lock()->KeyboardCaptivityChanged();
    return Status::OK;
  }

  virtual Status KeyboardReleased() override {
    is_keyboard_captive_ = false;
    if (!delegate_.expired()) delegate_.lock()->KeyboardCaptivityChanged();
    return Status::OK;
  }

  // BaseWindow::Server
  virtual Status SetSize(const Size& size) override {
    width_ = size.width;
    height_ = size.height;
    rebuild_texture_ = true;
    if (!delegate_.expired()) delegate_.lock()->WindowResized();
    return Status::OK;
  }

  virtual Status Closed() override {
    created_ = false;
    if (!delegate_.expired()) delegate_.lock()->WindowClosed();
    return Status::OK;
  }

  virtual Status GainedFocus() override {
    is_focused_ = true;
    if (!delegate_.expired()) delegate_.lock()->WindowFocusChanged();
    return Status::OK;
  }

  virtual Status LostFocus() override {
    is_focused_ = false;
    if (!delegate_.expired()) delegate_.lock()->WindowFocusChanged();
    return Status::OK;
  }

  virtual Status DisplayEnvironmentChanged() override { return Status::OK; }

 private:
  std::weak_ptr<WindowDelegate> delegate_;
  std::mutex mutex_;
  int width_;
  int height_;
  bool is_double_buffered_;
  int texture_id_;
  int frontbuffer_texture_id_;
  std::shared_ptr<SharedMemory> texture_shared_memory_;
  std::shared_ptr<SharedMemory> frontbuffer_shared_memory_;
  bool created_;
  bool rebuild_texture_;
  bool is_keyboard_captive_;
  bool is_mouse_captive_;
  bool is_focused_;

  void ReleaseTextures() {
    if (texture_id_ != 0) {
      // There is an old texture to release.
      GetService<GraphicsDevice>().DestroyTexture(
          graphics::TextureReference(texture_id_));
      texture_id_ = 0;
      texture_shared_memory_.reset();
    }

    if (frontbuffer_texture_id_ != 0) {
      // There is an old texture to release.
      GetService<GraphicsDevice>().DestroyTexture(
          graphics::TextureReference(frontbuffer_texture_id_));
      frontbuffer_texture_id_ = 0;
      frontbuffer_shared_memory_.reset();
    }
  }

  void RebuildTextures() {
    ReleaseTextures();

    // Create the back buffer that's drawn into (and also the front
    // buffer if not double buffered.)
    graphics::CreateTextureRequest request;
    request.size.width = width_;
    request.size.height = height_;
    auto status_or_response =
        GetService<GraphicsDevice>().CreateTexture(request);
    if (status_or_response) {
      texture_id_ = status_or_response->texture.id;
      texture_shared_memory_ = status_or_response->pixel_buffer;
    }

    if (is_double_buffered_) {
      // Create the front buffer to present.
      status_or_response = GetService<GraphicsDevice>().CreateTexture(request);
      if (status_or_response) {
        frontbuffer_texture_id_ = status_or_response->texture.id;
        frontbuffer_shared_memory_ = status_or_response->pixel_buffer;
      }
    }

    // Notify the window manager of the front buffer.
    SetWindowTextureParameters message;
    message.window = *this;
    message.texture.id =
        is_double_buffered_ ? frontbuffer_texture_id_ : texture_id_;
    GetService<WindowManager>().SetWindowTexture(message);
  }
};

}  // namespace

std::shared_ptr<Window> Window::CreateWindow(
    const Window::CreationOptions& creation_options) {
  auto window = std::make_shared<PerceptionWindow>();
  CreateWindowRequest create_window_request;
  create_window_request.window = *window;
  create_window_request.title = creation_options.title;
  create_window_request.keyboard_listener = *window;
  create_window_request.mouse_listener = *window;
  create_window_request.is_resizable = creation_options.is_resizable;
  create_window_request.desired_size.width = creation_options.prefered_width;
  create_window_request.hide_window_buttons =
      creation_options.hide_window_buttons;
  create_window_request.desired_size.height = creation_options.prefered_height;

  auto status_or_result =
      GetService<WindowManager>().CreateWindow(create_window_request);
  if (!status_or_result) {
    return nullptr;
  }

  window->SetInitialProperties(status_or_result->window_size.width,
                               status_or_result->window_size.height,
                               creation_options.is_double_buffered);
  return std::static_pointer_cast<Window>(window);
}

}  // namespace window
}  // namespace perception
#endif
