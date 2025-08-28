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
#include "perception/devices/mouse_listener.h"

#include "perception/serialization/serializer.h"

namespace perception {
namespace devices {

void RelativeMousePositionEvent::Serialize(
    serialization::Serializer& serializer) {
  serializer.Float("Delta X", delta_x);
  serializer.Float("Delta Y", delta_y);
}

void MousePositionEvent::Serialize(serialization::Serializer& serializer) {
  serializer.Float("X", x);
  serializer.Float("Y", y);
}

void MouseButtonEvent::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Button", button);
  serializer.Integer("Is Pressed Down", is_pressed_down);
}

void MouseClickEvent::Serialize(serialization::Serializer& serializer) {
  serializer.Serializable("Button", button);
  serializer.Serializable("Position", position);
}

}  // namespace devices
}  // namespace perception
