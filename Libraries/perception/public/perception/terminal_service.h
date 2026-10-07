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

#include "perception/serialization/serializable.h"
#include "perception/service_macros.h"
#include "types.h"

namespace perception {
namespace serialization {
class Serializer;
}

// Dimensions of a terminal window in characters and pixels.
class TerminalWindowSize : public serialization::Serializable {
 public:
  uint16 rows = 0;
  uint16 cols = 0;
  uint16 width_pixels = 0;
  uint16 height_pixels = 0;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

// POSIX-compatible terminal mode flags.
class TerminalAttributes : public serialization::Serializable {
 public:
  uint32 c_iflag = 0;
  uint32 c_oflag = 0;
  uint32 c_cflag = 0;
  uint32 c_lflag = 0;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

#define TERMINAL_SERVICE_METHOD_LIST(X)                   \
  X(1, GetWindowSize, TerminalWindowSize, void)           \
  X(2, SetWindowSize, void, TerminalWindowSize)           \
  X(3, GetAttributes, TerminalAttributes, void)           \
  X(4, SetAttributes, void, TerminalAttributes)
DEFINE_PERCEPTION_SERVICE(TerminalService, "perception.TerminalService",
                          TERMINAL_SERVICE_METHOD_LIST)
#undef TERMINAL_SERVICE_METHOD_LIST

}  // namespace perception
