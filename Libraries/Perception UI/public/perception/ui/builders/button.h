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
#include "perception/ui/components/button.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"

namespace perception {
namespace ui {
namespace builders {

// Creates a button node.
NODE_WITH_COMPONENT(Button, components::Button);

// Modifier to set the button's idle color.
COMPONENT_MODIFIER_1(ButtonIdleColor, components::Button, SetIdleColor, uint32);

// Modifier to set the button's hover color.
COMPONENT_MODIFIER_1(ButtonHoverColor, components::Button, SetHoverColor,
                     uint32);

// Modifier to set the button's pushed color.
COMPONENT_MODIFIER_1(ButtonPushedColor, components::Button, SetPushedColor,
                     uint32);

// Modifier to add a function to call when the button is pushed
COMPONENT_MODIFIER_1(OnPush, components::Button, OnPush, std::function<void()>);

// Standard button.
template <typename... Modifiers>
std::shared_ptr<perception::ui::Node> StandardButton(Modifiers... modifiers) {
  auto node = Button();
  Layout layout = node->GetLayout();
  layout.SetMinWidth(24.0f);
  layout.SetMinHeight(24.0f);
  layout.SetAlignItems(YGAlignCenter);
  layout.SetJustifyContent(YGJustifyCenter);
  ApplyModifiersToNode(*node, modifiers...);

  std::shared_ptr<components::Block> block =
      node->GetOrAdd<components::Block>();
  block->SetBorderRadius(6.0f);
  block->SetBorderWidth(1.0f);
  block->SetBorderColor(0xFF000000);
  return std::move(node);
}

}  // namespace builders
}  // namespace ui
}  // namespace perception
