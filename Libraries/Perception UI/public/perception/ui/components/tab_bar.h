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
#include <string>
#include <string_view>
#include <vector>

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "perception/type_id.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/node.h"
#include "perception/ui/theme.h"

namespace perception {
namespace ui {
namespace components {

class UiWindow;

class TabBar : public std::enable_shared_from_this<TabBar>,
               public UniqueIdentifiableType<TabBar> {
 public:
  TabBar();
  virtual ~TabBar();

  template <typename... Modifiers>
  static std::shared_ptr<Node> BasicTabBar(Node& window_node,
                                           Modifiers... modifiers) {
    std::weak_ptr<TabBar> weak_tab_bar;

    auto container = Block::SolidColor(
        kTitleBarUnfocusedBackgroundColor, &weak_tab_bar,
        [&window_node](Layout& layout) {
          layout.SetMinHeight(kTabBarHeight);
          layout.SetHeightAuto();
          layout.SetFlexDirection(YGFlexDirectionRow);
          layout.SetAlignItems(YGAlignCenter);
          layout.SetPadding(YGEdgeVertical, 0.0f);

          for (auto edge : {YGEdgeTop, YGEdgeLeft, YGEdgeRight})
            layout.SetMargin(edge, kTitleBarNegativeMargin);

          layout.SetPadding(YGEdgeRight,
                            RightPaddingForWindowNode(window_node));
        },
        [&window_node](TabBar& tab_bar) {
          tab_bar.HookUpWindowNode(window_node);
          tab_bar.SetTabsContainer(
              Container::HorizontalContainer([](Layout& layout) {
                layout.SetFlexGrow(1.0f);
                layout.SetFlexShrink(1.0f);
                layout.SetFlexDirection(YGFlexDirectionRow);
                layout.SetAlignItems(YGAlignFlexEnd);
                layout.SetAlignSelf(YGAlignStretch);
                layout.SetGap(0.0f);
              }));
        },
        [&weak_tab_bar](Node& node) {
          node.OnMouseButtonDown(
              [weak_tab_bar](const Point& p, window::MouseButton button) {
                if (button != window::MouseButton::Left) return;

                if (auto strong_tab_bar = weak_tab_bar.lock())
                  strong_tab_bar->HandleTitleBarClick(p);
              });

          node.OnDrawPostChildren(
              [weak_tab_bar](const DrawContext& draw_context) {
                if (auto strong_tab_bar = weak_tab_bar.lock())
                  strong_tab_bar->DrawPostChildren(draw_context);
              });
        },
        modifiers...);

    return container;
  }

  template <typename... Modifiers>
  static std::shared_ptr<Node> TabBarItem(std::weak_ptr<TabBar> weak_tab_bar,
                                          int index, bool is_active,
                                          Modifiers... modifiers) {
    return Node::Empty(
        [is_active](Layout& layout) {
          layout.SetHeight(is_active ? kTabBarActiveTabHeight
                                     : kTabBarInactiveTabHeight);
          layout.SetMinWidth(kTabBarMinTabWidth);
          layout.SetMaxWidth(kTabBarMaxTabWidth);
          layout.SetFlexGrow(1.0f);
          layout.SetFlexShrink(1.0f);
          layout.SetFlexDirection(YGFlexDirectionRow);
          layout.SetAlignItems(YGAlignCenter);
          layout.SetJustifyContent(YGJustifyCenter);
          layout.SetPadding(YGEdgeHorizontal, kTabBarTabHorizontalPadding);
        },
        [](Node& node) { node.SetCursor(window::Cursor::Poke); },
        [weak_tab_bar, index](Node& node) {
          node.OnDraw([weak_tab_bar, index](const DrawContext& draw_context) {
            if (auto strong_tab_bar = weak_tab_bar.lock())
              strong_tab_bar->DrawTabBarItem(index, draw_context);
          });
          node.OnMouseHover([weak_tab_bar, index](const Point&) {
            if (auto strong_tab_bar = weak_tab_bar.lock())
              strong_tab_bar->OnMouseHoverTabBarItem(index);
          });
          node.OnMouseLeave([weak_tab_bar, index]() {
            if (auto strong_tab_bar = weak_tab_bar.lock())
              strong_tab_bar->OnMouseLeaveTabBarItem(index);
          });
        },
        [weak_tab_bar, index, is_active](Node& node) {
          std::weak_ptr<Node> weak_node = node.ToSharedPtr();
          node.OnMouseButtonUp([weak_tab_bar, index, is_active, weak_node](
                                   const Point& p, window::MouseButton button) {
            if (button != window::MouseButton::Left) return;
            if (auto strong_tab_bar = weak_tab_bar.lock()) {
              if (is_active) {
                if (auto strong_node = weak_node.lock()) {
                  float tab_width = strong_node->GetSize().width;
                  if (p.x > tab_width - kTabBarCloseButtonWidth) return;
                }
              }
              strong_tab_bar->OnClickTabBarItem(index);
            }
          });
        },
        modifiers...);
  }

  void SetNode(std::weak_ptr<Node> node);

  void AddTab(std::string_view label);
  void RemoveTab(int index);
  void ClearTabs();
  void SelectTab(int index);
  void SetTabLabel(int index, std::string_view label);

  int GetTabCount() const;
  int GetSelectedTab() const;
  std::string_view GetTabLabel(int index) const;

  void OnTabSelected(std::function<void(int index)> handler);
  void OnTabClosed(std::function<void(int index)> handler);

  void SetPrefixNode(std::shared_ptr<Node> prefix_node);
  void SetSuffixNode(std::shared_ptr<Node> suffix_node);
  void HandleTitleBarClick(const Point& p);

  std::shared_ptr<Node> GetTabsContainer() const;
  void SetTabsContainer(std::shared_ptr<Node> tabs_container);

 private:
  struct Tab {
    std::string label;
    std::shared_ptr<Node> tab_node;
    uint32 background_color;
  };

  std::weak_ptr<Node> node_;
  std::weak_ptr<Node> window_node_;
  std::shared_ptr<Node> prefix_node_;
  std::shared_ptr<Node> suffix_node_;
  std::shared_ptr<Node> tabs_container_;

  std::vector<Tab> tabs_;
  int selected_tab_index_;

  std::vector<std::function<void(int)>> on_tab_selected_handlers_;
  std::vector<std::function<void(int)>> on_tab_closed_handlers_;

  void HookUpWindowNode(Node& window_node);
  void StartDraggingWindow();
  void WindowChangedFocus(UiWindow& window);
  static float RightPaddingForWindowNode(Node& window_node);

  void RebuildTabs();
  void DrawPostChildren(const DrawContext& draw_context);

  void DrawTabBarItem(int index, const DrawContext& draw_context);
  void OnMouseHoverTabBarItem(int index);
  void OnMouseLeaveTabBarItem(int index);
  void OnClickTabBarItem(int index);
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::TabBar>;

}  // namespace perception
