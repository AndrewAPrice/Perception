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

#include "perception/ui/components/title_bar.h"

#include <memory>

#include "perception/ui/components/ui_window.h"
#include "perception/ui/node.h"

namespace perception {
template class UniqueIdentifiableType<ui::components::TitleBar>;
namespace ui {
namespace components {

void TitleBar::SetNode(std::weak_ptr<Node> node) { node_ = node; }

void TitleBar::HookUpWindowNode(Node& window_node) {
  auto ui_window = window_node.Get<UiWindow>();
  if (!ui_window) return;

  window_node_ = window_node.ToSharedPtr();

  std::weak_ptr<TitleBar> weak_this = shared_from_this();
  ui_window->OnFocusChanged([weak_this]() {
    auto strong_this = weak_this.lock();
    if (!strong_this) return;

    auto strong_window_node = strong_this->window_node_.lock();
    if (!strong_window_node) return;

    strong_this->WindowChangedFocus(*strong_window_node->GetOrAdd<UiWindow>());
  });
}

void TitleBar::StartDraggingWindow() {
  auto strong_window_node = window_node_.lock();
  if (!strong_window_node) return;

  strong_window_node->GetOrAdd<UiWindow>()->StartDragging();
}

void TitleBar::SetTitleLabelNode(std::weak_ptr<Node> title_label_node) {
  title_label_node_ = title_label_node;
}

float TitleBar::RightPaddingForWindowNode(Node& window_node) {
  auto ui_window = window_node.Get<UiWindow>();
  if (!ui_window) return 6.f;

  return ui_window->IsResizable() ? 60.0f : (60.0f - 18.0f);
}

void TitleBar::WindowChangedFocus(UiWindow& window) {
  bool is_focused = window.IsFocused();

  // Change background color.
  auto node = node_.lock();
  if (node) {
    node->GetOrAdd<Block>()->SetFillColor(
        is_focused ? kTitleBarFocusedBackgroundColor
                   : kTitleBarUnfocusedBackgroundColor);
  }

  // Change text color.
  auto title_label_node = title_label_node_.lock();
  if (title_label_node) {
    title_label_node->GetOrAdd<Label>()->SetColor(
        is_focused ? kLabelOnDarkTextColor : kLabelTextColor);
  }
}

}  // namespace components
}  // namespace ui
}  // namespace perception
