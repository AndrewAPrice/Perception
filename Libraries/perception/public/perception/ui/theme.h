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

#include "types.h"

namespace perception {
namespace ui {

constexpr uint32 kBackgroundWindowColor = 0xc9fbffff;
constexpr uint32 kLabelTextColor = 0x000000ff;
constexpr uint32 kButtonBackgroundColor = 0x8cb7bfff;
constexpr uint32 kButtonPushedBackgroundColor = 0x779da3ff;
constexpr uint32 kButtonBrightColor = 0xb1e6f0ff;
constexpr uint32 kButtonDarkColor = 0x5f7d82ff;
constexpr uint32 kButtonTextColor = kLabelTextColor;
constexpr uint32 kTextBoxBackgroundColor = kButtonBackgroundColor;
constexpr uint32 kTextBoxTopLeftOutlineColor = kButtonDarkColor;
constexpr uint32 kTextBoxBottomRightOutlineColor = kButtonBrightColor;
constexpr uint32 kTextBoxTextColor = kLabelTextColor;
constexpr uint32 kTextBoxNonEditableTextColor = 0x111111ff;

}
}
