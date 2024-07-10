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

#include <memory>
#include <string_view>

#include "perception/ui/builders/macros.h"
#include "perception/ui/ui_window.h"

namespace perception {
namespace ui {
namespace builders {

// Creates a window node.
NODE_WITH_COMPONENT(Window, UiWindow);

// Modifier to set whether the window is a dialog.
COMPONENT_MODIFIER_1d(IsDialog, UiWindow, SetIsDialog, bool, true);

// Modifier to set the window background color.
COMPONENT_MODIFIER_1(WindowBackgroundColor, UiWindow, SetBackgroundColor,
                     uint32);

// Modifier to set the window's title.
COMPONENT_MODIFIER_1(WindowTitle, UiWindow, SetTitle, std::string_view);

// Modifier to set what handles when window resizes.
COMPONENT_MODIFIER_1(OnWindowResize, UiWindow, OnResize, std::function<void()>);

// Modifier to set what handles when the window closes.
COMPONENT_MODIFIER_1(OnWindowClose, UiWindow, OnClose, std::function<void()>);

}  // namespace builders
}  // namespace ui
}  // namespace perception
