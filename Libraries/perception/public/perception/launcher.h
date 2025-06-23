// Copyright 2021 Google LLC
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

#include <optional>
#include <string_view>

#include "perception/service_macros.h"
#include "types.h"

namespace perception {

#define METHOD_LIST(X) X(1, ShowLauncher, void, void)
DEFINE_PERCEPTION_SERVICE(Launcher, "perception.devices.ShowLauncher",
                          METHOD_LIST)
#undef METHOD_LIST

}  // namespace perception
