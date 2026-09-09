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

#include "perception/service_macros.h"

namespace perception {
namespace devices {

#define POWER_LISTENER_METHOD_LIST(X) \
  X(1, OnSleep, void, void)           \
  X(2, OnWake, void, void)

DEFINE_PERCEPTION_SERVICE(PowerListener, "perception.devices.PowerListener",
                          POWER_LISTENER_METHOD_LIST)
#undef POWER_LISTENER_METHOD_LIST

#define POWER_MANAGER_METHOD_LIST(X) \
  X(1, PowerOff, void, void)         \
  X(2, Restart, void, void)          \
  X(3, Sleep, void, void)            \
  X(4, Wake, void, void)

DEFINE_PERCEPTION_SERVICE(PowerManager, "perception.devices.PowerManager",
                          POWER_MANAGER_METHOD_LIST)
#undef POWER_MANAGER_METHOD_LIST

}  // namespace devices
}  // namespace perception
