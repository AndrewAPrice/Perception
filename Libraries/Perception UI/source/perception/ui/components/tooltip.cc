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

#include "perception/ui/components/tooltip.h"

#include "perception/ui/components/block.h"
#include "perception/ui/components/label.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/ui/theme.h"

namespace perception {
template class UniqueIdentifiableType<ui::components::Tooltip>;

namespace ui {
namespace components {

void Tooltip::Attach(std::shared_ptr<Node> target_node, std::string_view text) {
  if (!target_node) return;
  auto tooltip = target_node->GetOrAdd<Tooltip>();
  tooltip->SetText(text);
}

Tooltip::Tooltip() {}

void Tooltip::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  if (node.expired()) return;
  auto strong_node = node.lock();

  // Attach mouse event callbacks to show/hide tooltip
  strong_node->OnMouseHover([this](const Point& pos) { ShowTooltipAt(pos); });
  strong_node->OnMouseLeave([this]() { HideTooltip(); });
}

void Tooltip::SetText(std::string_view text) { text_ = text; }

std::string_view Tooltip::GetText() const { return text_; }

void Tooltip::ShowTooltipAt(const Point& mouse_pos) {
  if (node_.expired() || text_.empty()) return;
  auto strong_node = node_.lock();

  // Walk up to root node
  std::shared_ptr<Node> root = strong_node;
  while (true) {
    auto parent = root->GetParent().lock();
    if (!parent) break;
    root = parent;
  }

  Point target_abs = strong_node->GetAbsolutePosition();
  Point abs_mouse_pos = target_abs + mouse_pos;

  if (!tooltip_overlay_) {
    // Create the overlay node.
    tooltip_overlay_ = Node::Empty(
        [abs_mouse_pos](Layout& layout) {
          layout.SetPositionType(YGPositionTypeAbsolute);
          layout.SetPosition(YGEdgeLeft, abs_mouse_pos.x + kTooltipOffsetLeft);
          layout.SetPosition(YGEdgeTop, abs_mouse_pos.y + kTooltipOffsetTop);
        },
        [](Node& node) { node.SetBlocksHitTest(false); },
        Node::Empty(
            [](Layout& layout) {
              layout.SetPadding(YGEdgeAll, kTooltipPadding);
              layout.SetMaxWidth(kTooltipMaxWidth);
            },
            [](Block& block) {
              block.SetFillColor(kTooltipBackgroundColor);
              block.SetBorderColor(kTooltipBorderColor);
              block.SetBorderWidth(kTooltipBorderWidth);
              block.SetBorderRadius(kTooltipBorderRadius);
            },
            Label::BasicLabel(text_, [](Label& label) {
              label.SetColor(kTooltipTextColor);
            })));
    root->AddChild(tooltip_overlay_);
    root->Invalidate();
  }
}

void Tooltip::HideTooltip() {
  if (!tooltip_overlay_) return;

  auto root = tooltip_overlay_->GetParent().lock();
  if (root) {
    root->RemoveChild(tooltip_overlay_);
    root->Invalidate();
  }
  tooltip_overlay_.reset();
}

}  // namespace components
}  // namespace ui
}  // namespace perception
