// Copyright 2024 Google LLC
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

#include "perception/ui/measurements.h"

#include <algorithm>

#include "yoga/Yoga.h"

namespace perception {
namespace ui {

float CalculateMeasuredLength(YGMeasureMode mode, float requested_length,
                             float calculated_length) {

  if (mode == YGMeasureModeExactly) return requested_length;
  if (mode == YGMeasureModeAtMost)
    return std::min(requested_length, calculated_length);
  return calculated_length;
}

}  // namespace ui
}  // namespace perception
