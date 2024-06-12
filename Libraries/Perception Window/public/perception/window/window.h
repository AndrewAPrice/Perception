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
#pragma once

#include <memory>
#include <string>

namespace perception {
namespace window {

class WindowDelegate;

class Window {
 public:
  // Options for creating a window.
  struct CreationOptions {
   public:
    // The title of the window.
    std::string title;

    // The preferred width of the window. 0 means "don't care".
    int prefered_width = 0;

    // The preferred height of the window. 0 means "don't care".
    int prefered_height = 0;

    // Attempt to create the window full-screen.
    bool prefer_fullscreen;

    // Whether the window is resizable.
    bool is_resizable;

    // Whether the window is a dialog.
    bool is_dialog;

    // If the window is a dialog, the optional parent the dialog is for.
    Window* dialog_parent;

    // Whether the window will be double buffered. If double buffering is
    // disabled, the window manager/graphics driver could copy the pixels out of
    // the buffer while they're being written to. This could lead to tearing or
    // flickering, but is slightly faster.
    bool is_double_buffered = true;
  };
  // Creates a window. Can return a nullptr if something went wrong.
  static std::shared_ptr<Window> CreateWindow(
      const CreationOptions& creation_options);

  // Windows get closed upon destruction.
  virtual ~Window(){};

  // The delegate to send messages to.
  virtual void SetDelegate(std::weak_ptr<WindowDelegate> delegate) = 0;

  // Returns the width of the window.
  virtual int GetWidth() = 0;

  // Returns the height of the window.
  virtual int GetHeight() = 0;

  // Sets the desired size. The delegate will be notified if the window's size
  // changes.
  virtual void SetSize(int width, int height) = 0;

  // Returns the scale at which content on the window should be scaled to.
  virtual float GetScale() = 0;

  // Returns whether the window is full-screen.
  virtual bool IsFullScreen() = 0;

  // Sets whether the window should be full screen. The delegate will be
  // notified if the window has entered or left full screen.
  virtual void SetFullScreen(bool fullscreen) = 0;

  // Returns whether the mouse is captive.
  virtual bool IsMouseCaptive() = 0;

  // Sets whether the window should hold the mouse captive. A captive mouse
  // directly sends events such as movements and button changes to the window
  // rather than showing a pointer. The delegate will be notified if the mouse's
  // captivity state changes.
  virtual void SetCaptureMouse(bool capture) = 0;

  // Returns whether the keyboard is captive.
  virtual bool IsKeyboardCaptive() = 0;

  // Sets whether the window should hold the keyboard captive. A captive
  // keyboard sends events such as key strokes directly to the window, rather
  // than going through the window manager. The delegate will be notified if the
  // keyboard's captivity state changes.
  virtual void SetCaptureKeyboard(bool capture) = 0;

  // Returns whether the window is focused.
  virtual bool IsFocused() = 0;

  // Focuses the window. The delegate will be notified if the focal state of the
  // window changes.
  virtual void Focus() = 0;

  // Notifies that there's an update ready for thr window and the new contents
  // need to be presented. The delegate will be called to draw the actual
  // contents.
  virtual void Present() = 0;
};

}  // namespace window
}  // namespace perception
