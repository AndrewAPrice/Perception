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

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "perception/ui/components/block.h"
#include "perception/ui/components/label.h"
#include "perception/ui/node.h"
#include "perception/ui/theme.h"
#include "perception/type_id.h"

namespace perception {
namespace ui {
namespace components {

class UiWindow;

class TitleBar : public std::enable_shared_from_this<TitleBar>, public UniqueIdentifiableType<TitleBar> {
 public:
  template <typename... Modifiers>
  static std::shared_ptr<Node> TextTitleBar(std::string_view title,
                                            Node& window_node,
                                            Modifiers... modifiers) {
    auto title_label = Label::BasicLabel(
        title, [](Label& label) { label.SetColor(kLabelOnDarkTextColor); });

    std::weak_ptr<TitleBar> weak_title_bar;
    return Block::SolidColor(
        kTitleBarFocusedBackgroundColor, &weak_title_bar,
        [&window_node](Layout& layout) {
          layout.SetMinHeight(24.0f);
          layout.SetHeightAuto();
          layout.SetAlignItems(YGAlignFlexStart);
          for (auto edge : {YGEdgeTop, YGEdgeLeft, YGEdgeRight})
            layout.SetMargin(edge, -8.0f);
          for (auto edge : {YGEdgeTop, YGEdgeBottom, YGEdgeLeft})
            layout.SetPadding(edge, 8.0f);
          layout.SetPadding(YGEdgeRight,
                            RightPaddingForWindowNode(window_node));
        },
        [&window_node, &title_label](TitleBar& title_bar) {
          title_bar.HookUpWindowNode(window_node);
          title_bar.SetTitleLabelNode(title_label);
        },
        [&weak_title_bar](Node& node) {
          node.OnMouseButtonDown(
              [weak_title_bar](const Point&, window::MouseButton button) {
                if (button != window::MouseButton::Left) return;

                auto strong_title_bar = weak_title_bar.lock();
                if (strong_title_bar) strong_title_bar->StartDraggingWindow();
              });
        },
        title_label, modifiers...);
  }

  void SetNode(std::weak_ptr<Node> node);

 private:
  std::weak_ptr<Node> node_;
  std::weak_ptr<Node> window_node_;
  std::weak_ptr<Node> title_label_node_;

  void HookUpWindowNode(Node& window_node);
  void StartDraggingWindow();
  void SetTitleLabelNode(std::weak_ptr<Node> title_label_node);
  void WindowChangedFocus(UiWindow& window);
  static float RightPaddingForWindowNode(Node& window_node);
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::TitleBar>;

}  // namespace perception