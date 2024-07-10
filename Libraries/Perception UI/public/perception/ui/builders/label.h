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
#include "perception/ui/components/label.h"
#include "perception/ui/node.h"
#include "perception/ui/text_alignment.h"

namespace perception {
namespace ui {
namespace builders {

// Creates a label node.
NODE_WITH_COMPONENT(Label, components::Label);

// Modifier to set the text alignment.
COMPONENT_MODIFIER_1(AlignText, components::Label, SetTextAlignment, TextAlignment);

// Modifier to set the text.
COMPONENT_MODIFIER_1(Text, components::Label, SetText, std::string_view);

// Modifier to set the font.
COMPONENT_MODIFIER_1(Font, components::Label, SetFont, SkFont*);

// Modifier to set the text color.
COMPONENT_MODIFIER_1(TextColor, components::Label, SetColor, uint32_t);

}  // namespace builders
}  // namespace ui
}  // namespace perception
