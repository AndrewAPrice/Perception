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
#include "perception/ui/components/block.h"
#include "perception/ui/image_effect.h"
#include "perception/ui/node.h"
#include "perception/ui/text_alignment.h"

namespace perception {
namespace ui {
namespace builders {

// Creates a block node.
NODE_WITH_COMPONENT(Block, components::Block);

// Modifier to set the border color.
COMPONENT_MODIFIER_1(BorderColor, components::Block, SetBorderColor, uint32);

// Modifer to set the border width.
COMPONENT_MODIFIER_1(BorderWidth, components::Block, SetBorderWidth, float);

// Modifier to set the border radius.
COMPONENT_MODIFIER_1(BorderRadius, components::Block, SetBorderRadius, float);

// Modifier to set the fill color.
COMPONENT_MODIFIER_1(FillColor, components::Block, SetFillColor, uint32);

// Modifier to set whether to clip the contents.
COMPONENT_MODIFIER_1d(ClipContents, components::Block, SetClipContents, bool,
                      true);

// Modifier to set the image effect.
COMPONENT_MODIFIER_1(ImageEffect, components::Block, SetImageEffect,
                     std::shared_ptr<perception::ui::ImageEffect>);

}  // namespace builders
}  // namespace ui
}  // namespace perception
