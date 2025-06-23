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

class KeyboardEvent : public serialization::Serializable {
 public:
  uint8 key;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

#define METHOD_LIST(X)                   \
  X(1, KeyDown, void, KeyboardEvent)     \
  X(2, KeyUp, void, KeyboardEvent)       \
  X(3, KeyboardTakenCaptive, void, void) \
  X(4, KeyboardReleased, void, void)

DEFINE_PERCEPTION_SERVICE(KeyboardListener,
                          "perception.devices.KeyboardListener", METHOD_LIST)
#undef METHOD_LIST

}  // namespace devices
}  // namespace perception