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

#include "include/core/SkColor.h"
#include "types.h"

namespace perception {
namespace ui {

constexpr uint32 kBackgroundWindowColor =
    SkColorSetARGB(0xff, 0xc9, 0xfb, 0xff);
constexpr uint32 kLabelTextColor = SkColorSetARGB(0xff, 0x00, 0x00, 0x00);
constexpr uint32 kButtonBackgroundColor =
	SkColorSetARGB(0xff, 0x8c, 0xb7, 0xbf);
constexpr uint32 kButtonPushedBackgroundColor =
    SkColorSetARGB(0xff, 0x77, 0x9d, 0xa3);
constexpr uint32 kButtonBrightColor = SkColorSetARGB(0xff, 0xb1, 0xe6, 0xf0);
constexpr uint32 kButtonDarkColor = SkColorSetARGB(0xff, 0x5f, 0x7d, 0x82);
constexpr uint32 kButtonTextColor = kLabelTextColor;
constexpr uint32 kTextBoxBackgroundColor = kButtonBackgroundColor;
constexpr uint32 kTextBoxTopLeftOutlineColor = kButtonDarkColor;
constexpr uint32 kTextBoxBottomRightOutlineColor = kButtonBrightColor;
constexpr uint32 kTextBoxTextColor = kLabelTextColor;
constexpr uint32 kTextBoxNonEditableTextColor =
    SkColorSetARGB(0xff, 0x11, 0x11, 0x11);

}  // namespace ui
}  // namespace perception
