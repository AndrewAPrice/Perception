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

#include <memory>
#include <vector>

#include "perception/serialization/serializable.h"
#include "perception/service_macros.h"
#include "perception/shared_memory.h"

namespace perception {
namespace serialization {
class Serializer;
}

namespace devices {
namespace graphics {

class TextureReference : public serialization::Serializable {
 public:
  TextureReference() {}
  TextureReference(uint64 id) : id(id) {}
  uint64 id;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class Position : public serialization::Serializable {
 public:
  Position() {}
  Position(uint32 left, uint32 top) : left(left), top(top) {}
  uint32 left;
  uint32 top;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class Size : public serialization::Serializable {
 public:
  uint32 width;
  uint32 height;

  Size() {}
  Size(uint32 width, uint32 height) : width(width), height(height) {}

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class CopyPartOfTextureParameters : public serialization::Serializable {
 public:
  Position source;
  Position destination;
  Size size;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class FillRectangleParameters : public serialization::Serializable {
 public:
  Position destination;
  Size size;
  uint32 color;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class Command : public serialization::Serializable {
 public:
  enum class Type {
    SET_DESTINATION_TEXTURE,
    SET_SOURCE_TEXTURE,
    COPY_ENTIRE_TEXTURE,
    COPY_ENTIRE_TEXTURE_WITH_ALPHA_BLENDING,
    COPY_TEXTURE_TO_POSITION,
    COPY_TEXTURE_TO_POSITION_WITH_ALPHA_BLENDING,
    COPY_PART_OF_A_TEXTURE,
    COPY_PART_OF_A_TEXTURE_WITH_ALPHA_BLENDING,
    FILL_RECTANGLE
  };

  Type type;
  // One-of based on the type.

  // SET_DESTINATION_TEXTURE
  // SET_SOURCE_TEXTURE
  std::shared_ptr<TextureReference> texture_reference;

  // COPY_TEXTURE_TO_POSITION
  // COPY_TEXTURE_TO_POSITION_WITH_ALPHA_BLENDING
  std::shared_ptr<Position> position;

  // COPY_PART_OF_A_TEXTURE
  // COPY_PART_OF_A_TEXTURE_WITH_ALPHA_BLENDING
  std::shared_ptr<CopyPartOfTextureParameters> copy_part_of_texture_parameters;

  // FILL_RECTANGLE
  std::shared_ptr<FillRectangleParameters> fill_rectangle_parameters;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class Commands : public serialization::Serializable {
 public:
  std::vector<Command> commands;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class CreateTextureRequest : public serialization::Serializable {
 public:
  Size size;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class CreateTextureResponse : public serialization::Serializable {
 public:
  TextureReference texture;
  std::shared_ptr<SharedMemory> pixel_buffer;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class TextureInformation : public serialization::Serializable {
 public:
  ProcessId owner;
  Size size;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class ProcessAllowedToDrawToScreenParameters
    : public serialization::Serializable {
 public:
  ProcessId process;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

}  // namespace graphics

#define METHOD_LIST(X)                                      \
  X(1, RunCommands, void, graphics::Commands)               \
  X(2, CreateTexture, graphics::CreateTextureResponse,      \
    graphics::CreateTextureRequest)                         \
  X(3, DestroyTexture, void, graphics::TextureReference)    \
  X(4, GetTextureInformation, graphics::TextureInformation, \
    graphics::TextureReference)                             \
  X(5, SetProcessAllowedToDrawToScreen, void,               \
    graphics::ProcessAllowedToDrawToScreenParameters)       \
  X(6, GetScreenSize, graphics::Size, void)

DEFINE_PERCEPTION_SERVICE(GraphicsDevice, "perception.devices.GraphicsDevice",
                          METHOD_LIST)
#undef METHOD_LIST

}  // namespace devices
}  // namespace perception