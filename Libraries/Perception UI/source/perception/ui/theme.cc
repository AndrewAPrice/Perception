// Copyright 2022 Google LLC
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

#include "perception/ui/theme.h"

#include "include/core/SkColor.h"

namespace perception {
namespace ui {

inline uint32 HSVColor(float h, float s, float v) {
  float colors[3] = {h, s, v};
  return SkHSVToColor(colors);
}

uint32 kWidgetSpacing = 8.0f;

uint32 kBackgroundWindowColor = HSVColor(0.0f, 0.0f, 0.95f);
float kMarginAroundWidgets = 4.0f;

uint32 kLabelTextColor = SkColorSetARGB(0xff, 0x0, 0x0, 0x0);

uint32 kButtonBackgroundColor = HSVColor(0.0f, 0.0f, 1.0f);
uint32 kButtonBackgroundHoverColor = HSVColor(0.0f, 0.0f, 0.925f);
uint32 kButtonBackgroundPushedColor = HSVColor(0.0f, 0.0f, 0.9f);
uint32 kButtonOutlineColor = HSVColor(0.0f, 0.0f, 0.80f);
uint32 kButtonTextColor = kLabelTextColor;
float kButtonCornerRadius = 0.0f;

uint32 kTextBoxBackgroundColor = kButtonBackgroundColor;
uint32 kTextBoxOutlineColor = kButtonOutlineColor;
uint32 kTextBoxTextColor = kLabelTextColor;
float kTextBoxCornerRadius = kButtonCornerRadius;

uint32 kTextBoxNonEditableTextColor = SkColorSetARGB(0xff, 0x11, 0x11, 0x11);

uint32 kScrollBarFabBackgroundColor = HSVColor(0.0f, 0.0f, 0.9f);
uint32 kScrollBarFabBackgroundHoverColor = HSVColor(0.0f, 0.0f, 0.825f);
uint32 kScrollBarFabOutlineColor = HSVColor(0.0f, 0.0f, 0.8f);
uint32 kScrollBarTrackBackgroundColor = HSVColor(0.0f, 0.0f, 1.0f);
uint32 kScrollBarTrackBackgroundHoverColor = HSVColor(0.0f, 0.0f, 0.975f);
uint32 kScrollBarTrackOutlineColor = HSVColor(0.0f, 0.0f, 0.8f);
float kScrollBarThickness = 16.0f;
float kScrollBarBorderRadius = kScrollBarThickness / 2.0f;
float kScrollContainerBorderRadius = kScrollBarBorderRadius;

uint32 kScrollContainerOutlineColor = kScrollBarTrackOutlineColor;


uint32 kContainerBackgroundColor = HSVColor(0.0f, 0.0f, 0.97f);
uint32 kContainerBorderColor = kButtonOutlineColor;
float kContainerBorderWidth = 1.0f;
float kContainerBorderRadius = kButtonCornerRadius;
float kContainerPadding = kMarginAroundWidgets;

}  // namespace ui
}  // namespace perception