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
#include "perception/devices/graphics_device.h"

#include "perception/serialization/serializer.h"

namespace perception {
namespace devices {
namespace graphics {

void TextureReference::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Id", id);
}

void Position::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Left", left);
  serializer.Integer("Top", top);
}

void Size::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Width", width);
  serializer.Integer("Height", height);
}

void CopyPartOfTextureParameters::Serialize(serialization::Serializer& serializer) {
  serializer.Serializable("Source", source);
  serializer.Serializable("Destination", destination);
  serializer.Serializable("Size", size);
}

void FillRectangleParameters::Serialize(serialization::Serializer& serializer) {
  serializer.Serializable("Destination", destination);
  serializer.Serializable("Size", size);
  serializer.Integer("Color", color);
}

void Command::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Type", type);
  switch (type) {
    case Type::SET_DESTINATION_TEXTURE:
    case Type::SET_SOURCE_TEXTURE:
      serializer.Serializable("Texture reference", texture_reference);
      break;
    case Type::COPY_TEXTURE_TO_POSITION:
    case Type::COPY_TEXTURE_TO_POSITION_WITH_ALPHA_BLENDING:
      serializer.Serializable("Position", position);
      break;
    case Type::COPY_PART_OF_A_TEXTURE:
    case Type::COPY_PART_OF_A_TEXTURE_WITH_ALPHA_BLENDING:
      serializer.Serializable("Copy part of texture parameters",
                              copy_part_of_texture_parameters);
      break;
    case Type::FILL_RECTANGLE:
      serializer.Serializable("Fill rectangle parameters",
                              fill_rectangle_parameters);
      break;
    default:
      serializer.Serializable();
      break;
  }
}

void Commands::Serialize(serialization::Serializer& serializer) {
  serializer.ArrayOfSerializables("Commands", commands);
}

void CreateTextureRequest::Serialize(serialization::Serializer& serializer) {
  serializer.Serializable("Size", size);
}

void CreateTextureResponse::Serialize(serialization::Serializer& serializer) {
  serializer.Serializable("Texture", texture);
  serializer.Serializable("Pixel buffer", pixel_buffer);
}

void TextureInformation::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Owner", owner);
  serializer.Serializable("Size", size);
}

void ProcessAllowedToDrawToScreenParameters::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Process", process);
}

}  // namespace graphics

}  // namespace devices
}  // namespace perception
