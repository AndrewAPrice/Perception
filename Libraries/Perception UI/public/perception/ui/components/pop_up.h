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
#include <string_view>
#include <vector>

#include "perception/type_id.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/ui/theme.h"

namespace perception {
namespace ui {
namespace components {

class PopUp : public UniqueIdentifiableType<PopUp>,
              public std::enable_shared_from_this<PopUp> {
 public:
  PopUp();
  ~PopUp();

  // Shows a popup anchored at `anchor` (in window-absolute screen coordinates).
  // Popups close on outside clicks or when the window loses focus. Pop ups may
  // be repositioned or have vertical scroll bars added if they can not fit at
  // the given location or in the windowe
  static std::shared_ptr<PopUp> Show(std::shared_ptr<Node> context_node,
                                     Point anchor,
                                     std::shared_ptr<Node> content,
                                     std::function<void()> on_close = nullptr);

  // Closes the popup containing `node` (or any descendant inside the popup).
  static void Close(std::shared_ptr<Node> node);

  void Close();
  void SetNode(std::weak_ptr<Node> node);

 private:
  std::weak_ptr<Node> node_;
  std::weak_ptr<UiWindow> window_;
  uint64 focus_notify_id_;
  std::function<void()> on_close_;
  bool is_closed_;
};

class PopUpMenu {
 public:
  template <typename... Modifiers>
  static std::shared_ptr<Node> Container(Modifiers... modifiers) {
    return Node::Empty(
        [](Layout& layout) {
          layout.SetFlexDirection(YGFlexDirectionColumn);
          layout.SetPadding(YGEdgeAll, kPopUpMenuPadding);
          layout.SetMinWidth(120.0f);
        },
        [](Block& block) {
          block.SetFillColor(kPopUpMenuBackgroundColor);
          block.SetBorderColor(kPopUpMenuBorderColor);
          block.SetBorderWidth(kPopUpMenuBorderWidth);
          block.SetBorderRadius(kPopUpMenuBorderRadius);
        },
        modifiers...);
  }

  template <typename... Modifiers>
  static std::shared_ptr<Node> DropDownItem(std::string_view text,
                                            std::function<void()> on_click,
                                            Modifiers... modifiers) {
    return Button::TextButton(
        text,
        [on_click]() {
          if (on_click) on_click();
        },
        [](Block& block) {
          block.SetBorderWidth(0.0f);
          block.SetBorderRadius(4.0f);
        },
        [](Button& btn) {
          btn.SetIdleColor(0x00000000);
          btn.SetHoverColor(0xFFE5E7EB);
          btn.SetPushedColor(0xFFD1D5DB);
          btn.SetLabelColor(0xFF111827);
        },
        [](Layout& layout) {
          layout.SetWidthPercent(100.f);
          layout.SetHeight(22.0f);
          layout.SetMinHeight(22.0f);
          layout.SetAlignItems(YGAlignFlexStart);
          layout.SetJustifyContent(YGJustifyCenter);
          layout.SetPadding(YGEdgeHorizontal, 8.0f);
        },
        modifiers...,
        [](Button& btn) {
          btn.OnPush([btn_weak = btn.GetNode()]() {
            if (!btn_weak.expired()) {
              PopUp::Close(btn_weak.lock());
            }
          });
        });
  }

  template <typename... Modifiers>
  static std::shared_ptr<Node> ContextMenuItem(std::string_view text,
                                               std::function<void()> on_click,
                                               Modifiers... modifiers) {
    return Button::TextButton(
        text,
        [on_click]() {
          if (on_click) on_click();
        },
        [](Block& block) {
          block.SetBorderWidth(0.0f);
          block.SetBorderRadius(4.0f);
        },
        [](Button& btn) {
          btn.SetIdleColor(0x00000000);
          btn.SetHoverColor(0xFFE5E7EB);
          btn.SetPushedColor(0xFFD1D5DB);
          btn.SetLabelColor(0xFF111827);
        },
        [](Layout& layout) {
          layout.SetWidthPercent(100.f);
          layout.SetHeight(28.0f);
          layout.SetMinHeight(28.0f);
          layout.SetAlignItems(YGAlignFlexStart);
          layout.SetJustifyContent(YGJustifyCenter);
          layout.SetPadding(YGEdgeHorizontal, 8.0f);
        },
        modifiers...,
        [](Button& btn) {
          btn.OnPush([btn_weak = btn.GetNode()]() {
            if (!btn_weak.expired()) {
              PopUp::Close(btn_weak.lock());
            }
          });
        });
  }
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::PopUp>;

}  // namespace perception
