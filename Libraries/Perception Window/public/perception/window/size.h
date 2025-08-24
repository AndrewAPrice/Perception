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

#include "perception/serialization/serializable.h"

namespace perception {
namespace serialization {
class Serializer;
}

namespace window {

class Size : public serialization::Serializable {
 public:
  Size() : width(0), height(0) {}
  Size(float width, float height) : width(width), height(height) {}

  float width, height;

  virtual void Serialize(serialization::Serializer &serializer) override;
};

}  // namespace window
}  // namespace perception