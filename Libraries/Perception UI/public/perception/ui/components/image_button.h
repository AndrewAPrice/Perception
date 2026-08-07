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

#include <functional>
#include <memory>

#include "perception/type_id.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/image_view.h"
#include "perception/ui/image.h"
#include "perception/ui/node.h"
#include "perception/ui/theme.h"

namespace perception {
namespace ui {
namespace components {

class ImageButton : public UniqueIdentifiableType<ImageButton> {
 public:
  // Creates an image button with an image parameter and custom modifiers.
  template <typename... Modifiers>
  static std::shared_ptr<Node> BasicImageButton(std::function<void()> on_push,
                                                std::shared_ptr<Image> image,
                                                Modifiers... modifiers) {
    auto image_node = image
                          ? ImageView::BasicImage(
                                image,
                                [](Layout& l) {
                                  l.SetWidthPercent(100.0f);
                                  l.SetHeightPercent(100.0f);
                                },
                                [](ImageView& iv) {
                                  iv.SetResizeMethod(ResizeMethod::Contain);
                                  iv.SetAlignment(TextAlignment::MiddleCenter);
                                })
                          : Node::Empty();

    return Node::Empty([](ImageButton& image_button) {},
                       [on_push](Button& button) {
                         button.OnPush(on_push);
                         button.SetButtonStyle(Button::ButtonStyle::GHOST);
                       },
                       [](Layout& layout) {
                         layout.SetWidth(kImageButtonWidth);
                         layout.SetHeight(kImageButtonHeight);
                         layout.SetAlignItems(YGAlignCenter);
                         layout.SetJustifyContent(YGJustifyCenter);
                       },
                       [](Block& block) {
                         block.SetBorderRadius(kButtonBorderRadius);
                         block.SetBorderWidth(0.0f);
                       },
                       image_node, modifiers...);
  }

  ImageButton();

  void SetNode(std::weak_ptr<Node> node);
  std::weak_ptr<Node> GetNode() const;

 private:
  std::weak_ptr<Node> node_;
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::ImageButton>;

}  // namespace perception
