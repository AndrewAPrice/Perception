// Copyright 2026 Google LLC
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

#include "perception/devices/mouse_listener.h"
#include "perception/serialization/serializable.h"
#include "perception/service_macros.h"

namespace perception {
namespace devices {

// Normalized position event where x and y are in range [0.0, 1.0].
class TabletHoverEvent : public serialization::Serializable {
 public:
  float x, y;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

#define METHOD_LIST(X)                      \
  X(1, TabletHover, void, TabletHoverEvent) \
  X(2, TabletButton, void, MouseButtonEvent)

DEFINE_PERCEPTION_SERVICE(TabletListener, "perception.devices.TabletListener",
                          METHOD_LIST)
#undef METHOD_LIST

}  // namespace devices
}  // namespace perception
