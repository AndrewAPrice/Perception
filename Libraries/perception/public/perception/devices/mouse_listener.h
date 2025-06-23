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
// #define PERCEPTION
#pragma once

#include "perception/serialization/serializable.h"
#include "perception/service_macros.h"

namespace perception {
namespace serialization {
class Serializer;
}

namespace devices {

enum class MouseButton : uint8 { Unknown, Left, Middle, Right };

class RelativeMousePositionEvent : public serialization::Serializable {
 public:
  float delta_x, delta_y;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class MousePositionEvent : public serialization::Serializable {
 public:
  float x, y;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class MouseButtonEvent : public serialization::Serializable {
 public:
  MouseButton button;
  bool is_pressed_down;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class MouseClickEvent : public serialization::Serializable {
 public:
  MouseButtonEvent button;
  MousePositionEvent position;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

#define METHOD_LIST(X)                                \
  X(1, MouseMove, void, RelativeMousePositionEvent)   \
  X(2, MouseScroll, void, RelativeMousePositionEvent) \
  X(3, MouseButton, void, MouseButtonEvent)           \
  X(4, MouseClick, void, MouseClickEvent)             \
  X(5, MouseEnter, void, void)                        \
  X(6, MouseLeave, void, void)                        \
  X(7, MouseHover, void, MousePositionEvent)          \
  X(8, MouseTakenCaptive, void, void)                 \
  X(9, MouseReleased, void, void)

DEFINE_PERCEPTION_SERVICE(MouseListener, "perception.devices.MouseListener",
                          METHOD_LIST)
#undef METHOD_LIST

}  // namespace devices
}  // namespace perception