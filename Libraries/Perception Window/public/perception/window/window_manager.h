// Copyright 2025 Google LLC
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

#include <string>

#include "perception/devices/graphics_device.h"
#include "perception/devices/keyboard_listener.h"
#include "perception/devices/mouse_listener.h"
#include "perception/serialization/serializable.h"
#include "perception/service_macros.h"
#include "perception/window/base_window.h"
#include "perception/window/size.h"

namespace perception {
namespace serialization {
class Serializer;
}
namespace window {

class CreateWindowRequest : public serialization::Serializable {
 public:
  // The window service to open.
  BaseWindow::Client window;

  // The title of the window.
  std::string title;

  // Whether this window should be resizable.
  bool is_resizable;

  // Whether the window buttons should hide if they're not being hovered over.
  bool hide_window_buttons;

  // The desired size of the window. If this is {0,0} then the window will
  // automatically be sized. If this is larger than the maximum window size,
  // then the maximum window size will be used.
  Size desired_size;

  // The keyboard listener, if this window cares about keyboard events.
  devices::KeyboardListener::Client keyboard_listener;

  // The mouse listener, if this window cares about mouse events.
  devices::MouseListener::Client mouse_listener;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

// Represents the screen's color space.
class ColorSpace : public serialization::Serializable {
 public:
  class Value : public serialization::Serializable {
   public:
    float value;
    virtual void Serialize(serialization::Serializer& serializer) override;
  };

  // 7-d vector describing how to linearize the gamma.
  std::vector<Value> transfer_function;

  // A 3x3 matrix to convert the color to XYZ D50 color space.
  std::vector<Value> matrix;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class DisplayEnvironment : public serialization::Serializable {
 public:
  Size screen_size;

  float scale;

  ColorSpace color_space;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class CreateWindowResponse : public serialization::Serializable {
 public:
  Size window_size;
  DisplayEnvironment display_environment;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class SetWindowTextureParameters : public serialization::Serializable {
 public:
  BaseWindow::Client window;
  devices::graphics::TextureReference texture;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class SetWindowTitleParameters : public serialization::Serializable {
 public:
  BaseWindow::Client window;
  std::string title;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class InvalidateWindowParameters : public serialization::Serializable {
 public:
  BaseWindow::Client window;
  float left, top, right, bottom;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

#define METHOD_LIST(X)                                          \
  X(1, CreateWindow, CreateWindowResponse, CreateWindowRequest) \
  X(2, CloseWindow, void, BaseWindow::Client)                   \
  X(3, SetWindowTexture, void, SetWindowTextureParameters)      \
  X(4, SetWindowTitle, void, SetWindowTitleParameters)          \
  X(5, SystemButtonPushed, void, void)                          \
  X(6, InvalidateWindow, void, InvalidateWindowParameters)      \
  X(7, GetMaximumWindowSize, Size, void)                        \
  X(8, GetDisplayEnvironment, DisplayEnvironment, void)
DEFINE_PERCEPTION_SERVICE(WindowManager, "perception.window.WindowManager",
                          METHOD_LIST)
#undef METHOD_LIST

}  // namespace window
}  // namespace perception