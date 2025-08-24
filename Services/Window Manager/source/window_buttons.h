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

#include <optional>

#include "status.h"

namespace perception {
namespace ui {
struct Point;
struct Size;
}  // namespace ui
}  // namespace perception

enum class WindowButton {
  // Button to close the window.
  Close,
  // Button to minimize the window.
  Minimize,
  // Button to toggle full screen.
  ToggleFullScreen
};

// Initializes the asset for the window buttons.
::perception::Status InitializeWindowButtons();

// Returns the ID of the texture that contains the window button.
int WindowButtonsTextureId();

// Returns the size of the window buttons area.
::perception::ui::Size WindowButtonSize(bool is_resizable);

// Returns the offset in the window button texture to draw the particular
// configuration.
::perception::ui::Point WindowButtonTextureOffset(
    bool is_resizable, const std::optional<WindowButton>& selected_button);

// Gets the window button that may be under a point in the window. Make sure the
// point lies within the window buttons bounds.
WindowButton GetWindowButtonAtPoint(int x, bool is_resizable);