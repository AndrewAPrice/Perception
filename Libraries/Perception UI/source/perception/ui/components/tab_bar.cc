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

#include "perception/ui/components/tab_bar.h"

#include "include/core/SkRRect.h"
#include "perception/debug.h"
#include "perception/scheduler.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/node.h"
#include "perception/ui/rectangle.h"
#include "perception/ui/theme.h"

namespace perception {
template class UniqueIdentifiableType<ui::components::TabBar>;
namespace ui {
namespace components {

TabBar::TabBar() : selected_tab_index_(-1) {}

TabBar::~TabBar() {}

void TabBar::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  if (node.expired()) {
    return;
  }
  auto strong_node = node.lock();
  strong_node->SetBlocksHitTest(true);

  RebuildTabs();
}

void TabBar::HookUpWindowNode(Node& window_node) {
  auto ui_window = window_node.Get<UiWindow>();
  if (!ui_window) return;

  window_node_ = window_node.ToSharedPtr();

  std::weak_ptr<TabBar> weak_this = shared_from_this();
  ui_window->OnFocusChanged([weak_this]() {
    auto strong_this = weak_this.lock();
    if (!strong_this) return;

    auto strong_window_node = strong_this->window_node_.lock();
    if (!strong_window_node) return;

    strong_this->WindowChangedFocus(*strong_window_node->GetOrAdd<UiWindow>());
  });
}

void TabBar::StartDraggingWindow() {
  auto strong_window = window_node_.lock();
  if (!strong_window) return;

  strong_window->GetOrAdd<UiWindow>()->StartDragging();
}

float TabBar::RightPaddingForWindowNode(Node& window_node) {
  auto ui_window = window_node.Get<UiWindow>();
  if (!ui_window) return kTitleBarRightPaddingWithoutButtons;

  return ui_window->IsResizable()
             ? kTitleBarRightPaddingWithResizableButtons
             : kTitleBarRightPaddingWithNonResizableButtons;
}

void TabBar::WindowChangedFocus(UiWindow& window) {
  bool is_focused = window.IsFocused();

  if (auto node = node_.lock()) {
    node->GetOrAdd<Block>()->SetFillColor(
        is_focused ? kTitleBarFocusedBackgroundColor
                   : kTitleBarUnfocusedBackgroundColor);
  }

  // Re-render active/inactive tab colors to match window focus state
  for (int i = 0; i < (int)tabs_.size(); i++) {
    auto tab_node = tabs_[i].tab_node;
    if (!tab_node) continue;

    if (i == selected_tab_index_) {
      tabs_[i].background_color = is_focused
                                      ? kTabBarActiveFocusedBackgroundColor
                                      : kTabBarActiveUnfocusedBackgroundColor;
    } else {
      tabs_[i].background_color = kTabBarInactiveBackgroundColor;
    }
    tab_node->Invalidate();

    // Note: We can search children for active color modifications
    for (auto& child : tab_node->GetChildren()) {
      if (auto child_lbl = child->Get<Label>()) {
        if (i == selected_tab_index_) {
          child_lbl->SetColor(kTabBarActiveTextColor);
        } else {
          child_lbl->SetColor(kTabBarInactiveTextColor);
        }
      }
    }
  }
}

void TabBar::AddTab(std::string_view label) {
  Tab new_tab;
  new_tab.label = std::string(label);
  tabs_.push_back(new_tab);

  if (selected_tab_index_ == -1) {
    selected_tab_index_ = 0;
  }

  RebuildTabs();
}

void TabBar::RemoveTab(int index) {
  if (index < 0 || index >= (int)tabs_.size()) return;

  tabs_.erase(tabs_.begin() + index);

  if (tabs_.empty()) {
    selected_tab_index_ = -1;
  } else if (selected_tab_index_ >= (int)tabs_.size()) {
    selected_tab_index_ = (int)tabs_.size() - 1;
  }

  RebuildTabs();
}

void TabBar::ClearTabs() {
  tabs_.clear();
  selected_tab_index_ = -1;
  RebuildTabs();
}

void TabBar::SelectTab(int index) {
  if (index < 0 || index >= (int)tabs_.size() || index == selected_tab_index_)
    return;

  selected_tab_index_ = index;
  RebuildTabs();
}

void TabBar::SetTabLabel(int index, std::string_view label) {
  if (index < 0 || index >= (int)tabs_.size()) return;
  tabs_[index].label = std::string(label);

  // Update the existing label directly if possible to preserve state
  if (tabs_[index].tab_node) {
    for (auto& child : tabs_[index].tab_node->GetChildren()) {
      if (auto child_lbl = child->Get<Label>()) {
        // Verify it is not the close button (which is "x")
        if (child_lbl->GetText() != "x") {
          child_lbl->SetText(tabs_[index].label);
          break;
        }
      }
    }
  }
}

int TabBar::GetTabCount() const { return (int)tabs_.size(); }

int TabBar::GetSelectedTab() const { return selected_tab_index_; }

std::string_view TabBar::GetTabLabel(int index) const {
  if (index < 0 || index >= (int)tabs_.size()) return "";
  return tabs_[index].label;
}

void TabBar::OnTabSelected(std::function<void(int)> handler) {
  on_tab_selected_handlers_.push_back(handler);
}

void TabBar::OnTabClosed(std::function<void(int)> handler) {
  on_tab_closed_handlers_.push_back(handler);
}

void TabBar::SetPrefixNode(std::shared_ptr<Node> prefix_node) {
  prefix_node_ = prefix_node;
  RebuildTabs();
}

void TabBar::SetSuffixNode(std::shared_ptr<Node> suffix_node) {
  suffix_node_ = suffix_node;
  RebuildTabs();
}

std::shared_ptr<Node> TabBar::GetTabsContainer() const {
  return tabs_container_;
}

void TabBar::SetTabsContainer(std::shared_ptr<Node> tabs_container) {
  if (tabs_container_ == tabs_container) return;
  tabs_container_ = tabs_container;
  RebuildTabs();
}

void TabBar::RebuildTabs() {
  auto strong_node = node_.lock();
  if (!strong_node) return;

  if (!tabs_container_) return;

  strong_node->RemoveChildren();
  tabs_container_->RemoveChildren();

  if (prefix_node_) strong_node->AddChild(prefix_node_);

  strong_node->AddChild(tabs_container_);

  bool is_window_focused = true;
  if (!window_node_.expired()) {
    auto window = window_node_.lock()->Get<UiWindow>();
    if (window) {
      is_window_focused = window->IsFocused();
    }
  }

  // Construct the tabs.
  for (int i = 0; i < (int)tabs_.size(); i++) {
    bool is_active = (i == selected_tab_index_);
    std::string label_text = tabs_[i].label;

    tabs_[i].background_color =
        is_active ? (is_window_focused ? kTabBarActiveFocusedBackgroundColor
                                       : kTabBarActiveUnfocusedBackgroundColor)
                  : kTabBarInactiveBackgroundColor;

    auto tab_lbl =
        Label::SingleLineTruncated(label_text, [is_active](Label& lbl) {
          lbl.SetColor(is_active ? kTabBarActiveTextColor
                                 : kTabBarInactiveTextColor);
        });

    std::shared_ptr<Node> tab_item;
    if (is_active) {
      auto close_btn = Label::SingleLineTruncated(
          "x",
          [](Layout& layout) {
            layout.SetMargin(YGEdgeLeft, kTabBarCloseButtonMarginLeft);
            layout.SetPadding(YGEdgeAll, kTabBarCloseButtonPadding);
          },
          [](Label& lbl) { lbl.SetColor(kTabBarCloseButtonColor); });

      // Close button interaction
      std::weak_ptr<Node> weak_close = close_btn;
      close_btn->OnMouseHover([weak_close](const Point&) {
        auto strong_close = weak_close.lock();
        if (!strong_close) return;
        if (auto lbl = strong_close->Get<Label>()) {
          lbl->SetColor(kTabBarCloseButtonHoverColor);
        }
      });

      close_btn->OnMouseLeave([weak_close]() {
        auto strong_close = weak_close.lock();
        if (!strong_close) return;
        if (auto lbl = strong_close->Get<Label>()) {
          lbl->SetColor(kTabBarCloseButtonColor);
        }
      });

      close_btn->OnMouseButtonUp(
          [this, i](const Point&, window::MouseButton button) {
            if (button != window::MouseButton::Left) return;
            // Prevent click propagating to tab select
            for (auto& handler : on_tab_closed_handlers_) {
              ::perception::DeferAfterEvents([handler, i]() { handler(i); });
            }
          });

      tab_item =
          TabBarItem(shared_from_this(), i, is_active, tab_lbl, close_btn);
    } else {
      tab_item = TabBarItem(shared_from_this(), i, is_active, tab_lbl);
    }

    tabs_[i].tab_node = tab_item;
    tabs_container_->AddChild(tab_item);
  }

  // 4. Add Suffix Node (if any)
  if (suffix_node_) {
    strong_node->AddChild(suffix_node_);
  }

  // Trigger layout update
  strong_node->Invalidate();
}

void TabBar::HandleTitleBarClick(const Point& p) {
  // Ignore clicks in suffix node.
  if (suffix_node_ && suffix_node_->GetAreaRelativeToParent().Contains(p))
    return;

  // Ignore clicks in any tab node.
  if (tabs_container_) {
    Point container_origin = tabs_container_->GetAreaRelativeToParent().origin;
    for (const auto& tab : tabs_) {
      if (tab.tab_node) {
        Rectangle tab_rect{
            .origin = container_origin +
                      tab.tab_node->GetAreaRelativeToParent().origin,
            .size = tab.tab_node->GetSize()};
        if (tab_rect.Contains(p)) return;  // Clicked tab bar.
      }
    }
  }

  // Click was in empty title bar space, start dragging window.
  StartDraggingWindow();
}

void TabBar::DrawPostChildren(const DrawContext& draw_context) {
  float container_x = draw_context.area.origin.x;
  float container_y = draw_context.area.origin.y;
  float container_width = draw_context.area.size.width;
  float container_height = draw_context.area.size.height;

  float line_y = container_y + container_height - kTabBarBottomLineThickness;

  SkPaint paint;
  paint.setColor(kTabBarBottomLineColor);
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(kTabBarBottomLineThickness);

  int active_idx = GetSelectedTab();
  if (active_idx >= 0 && active_idx < GetTabCount()) {
    auto active_tab_node = tabs_[active_idx].tab_node;
    auto tabs_container = tabs_container_;
    if (active_tab_node && tabs_container) {
      float active_tab_left =
          tabs_container->GetAreaRelativeToParent().origin.x +
          active_tab_node->GetAreaRelativeToParent().origin.x;
      float active_tab_right =
          active_tab_left + active_tab_node->GetSize().width;

      float abs_active_tab_left = container_x + active_tab_left;
      float abs_active_tab_right = container_x + active_tab_right;

      if (abs_active_tab_left > container_x) {
        draw_context.skia_canvas->drawLine(container_x, line_y,
                                           abs_active_tab_left, line_y, paint);
      }
      if (abs_active_tab_right < container_x + container_width) {
        draw_context.skia_canvas->drawLine(abs_active_tab_right, line_y,
                                           container_x + container_width,
                                           line_y, paint);
      }
      return;
    }
  }

  draw_context.skia_canvas->drawLine(
      container_x, line_y, container_x + container_width, line_y, paint);
}

void TabBar::DrawTabBarItem(int index, const DrawContext& draw_context) {
  float x = draw_context.area.origin.x;
  float y = draw_context.area.origin.y;
  float width = draw_context.area.size.width;
  float height = draw_context.area.size.height;

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(tabs_[index].background_color);
  paint.setStyle(SkPaint::kFill_Style);

  float r = kTabBarTabCornerRadius;
  SkVector radii[4] = {
      {r, r},        // Upper left
      {r, r},        // Upper right
      {0.0f, 0.0f},  // Lower right
      {0.0f, 0.0f}   // Lower left
  };
  SkRRect rrect;
  rrect.setRectRadii(SkRect::MakeXYWH(x, y, width, height), radii);
  draw_context.skia_canvas->drawRRect(rrect, paint);
}

void TabBar::OnMouseHoverTabBarItem(int index) {
  tabs_[index].background_color = kTabBarHoverBackgroundColor;
  if (tabs_[index].tab_node) tabs_[index].tab_node->Invalidate();
}

void TabBar::OnMouseLeaveTabBarItem(int index) {
  tabs_[index].background_color = kTabBarInactiveBackgroundColor;
  if (tabs_[index].tab_node) tabs_[index].tab_node->Invalidate();
}

void TabBar::OnClickTabBarItem(int index) {
  ::perception::DeferAfterEvents([this, index]() {
    SelectTab(index);
    for (auto& handler : on_tab_selected_handlers_) {
      handler(index);
    }
  });
}

}  // namespace components
}  // namespace ui
}  // namespace perception
