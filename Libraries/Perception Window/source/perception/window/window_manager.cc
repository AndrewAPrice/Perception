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
#include "perception/window/window_manager.h"

#include "perception/serialization/serializer.h"

namespace perception {
namespace window {

void CreateWindowRequest::Serialize(serialization::Serializer& serializer) {
  serializer.Serializable("Window", window);
  serializer.String("Title", title);
  serializer.Integer("Is resizable", is_resizable);
  serializer.Integer("Hide window buttons", hide_window_buttons);
  serializer.Serializable("Desired size", desired_size);
  serializer.Serializable("Keyboard listener", keyboard_listener);
  serializer.Serializable("Mouse listener", mouse_listener);
}

void ColorSpace::Serialize(serialization::Serializer& serializer) {
  serializer.ArrayOfSerializables("Transfer function", transfer_function);
  serializer.ArrayOfSerializables("Matrix", matrix);
}

void ColorSpace::Value::Serialize(serialization::Serializer& serializer) {
  serializer.Float("Value", value);
}

void DisplayEnvironment::Serialize(serialization::Serializer& serializer) {
  serializer.Serializable("Screen size", screen_size);
  serializer.Float("Scale", scale);
  serializer.Serializable("Color space", color_space);
}

void CreateWindowResponse::Serialize(serialization::Serializer& serializer) {
  serializer.Serializable("Window size", window_size);
  serializer.Serializable("Display environment", display_environment);
}

void SetWindowTextureParameters::Serialize(
    serialization::Serializer& serializer) {
  serializer.Serializable("Window", window);
  serializer.Serializable("Texture", texture);
}

void SetWindowTitleParameters::Serialize(
    serialization::Serializer& serializer) {
  serializer.Serializable("Window", window);
  serializer.String("Title", title);
}

void InvalidateWindowParameters::Serialize(
    serialization::Serializer& serializer) {
  serializer.Serializable("Window", window);
  serializer.Float("Left", left);
  serializer.Float("Top", top);
  serializer.Float("Right", right);
  serializer.Float("Bottom", bottom);
}

}  // namespace window
}  // namespace perception