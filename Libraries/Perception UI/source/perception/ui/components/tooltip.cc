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

std::weak_ptr<Tooltip> Tooltip::active_tooltip_;

void Tooltip::Attach(std::shared_ptr<Node> target_node, std::string_view text) {
  if (!target_node) return;
  auto tooltip = target_node->GetOrAdd<Tooltip>();
  tooltip->SetText(text);
}

Tooltip::Tooltip() {}

Tooltip::~Tooltip() {
  HideTooltip();
}

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

  if (auto active = active_tooltip_.lock()) {
    if (active.get() != this) {
      active->HideTooltip();
    }
  }

  // Walk up to root node
  std::shared_ptr<Node> root = strong_node;
  while (true) {
    auto parent = root->GetParent().lock();
    if (!parent) break;
    root = parent;
  }

  Point target_abs = strong_node->GetAbsolutePosition();
  Point abs_mouse_pos = target_abs + mouse_pos;

  if (tooltip_overlay_) {
    HideTooltip();
  }

  if (!tooltip_overlay_) {
    // Create tooltip content node first to measure its size.
    auto tooltip_content = Node::Empty(
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
        }));

    tooltip_content->GetLayout().Calculate(YGUndefined, YGUndefined);
    float content_w =
        tooltip_content->GetLayout().GetCalculatedWidthWithMargin();
    float content_h =
        tooltip_content->GetLayout().GetCalculatedHeightWithMargin();

    float window_w = root->GetLayout().GetCalculatedWidth();
    float window_h = root->GetLayout().GetCalculatedHeight();
    if (window_w <= 0.0f || window_h <= 0.0f) {
      auto sz = root->GetSize();
      window_w = sz.width;
      window_h = sz.height;
    }

    float posX = abs_mouse_pos.x + kTooltipOffsetLeft;
    if (window_w > 0.0f && posX + content_w > window_w) {
      posX = window_w - content_w;
    }
    if (posX < 0.0f) posX = 0.0f;

    float posY = abs_mouse_pos.y + kTooltipOffsetTop;
    if (window_h > 0.0f && posY + content_h > window_h) {
      posY = abs_mouse_pos.y - content_h - kTooltipOffsetTop;
      if (posY + content_h > window_h) {
        posY = std::max(0.0f, window_h - content_h);
      }
    }
    if (posY < 0.0f) posY = 0.0f;

    // Create the overlay node.
    tooltip_overlay_ = Node::Empty(
        [posX, posY](Layout& layout) {
          layout.SetPositionType(YGPositionTypeAbsolute);
          layout.SetPosition(YGEdgeLeft, posX);
          layout.SetPosition(YGEdgeTop, posY);
        },
        [](Node& node) { node.SetBlocksHitTest(false); },
        tooltip_content);
    root->AddChild(tooltip_overlay_);
    root->Invalidate();
    active_tooltip_ = shared_from_this();
  }
}

void Tooltip::HideTooltip() {
  if (tooltip_overlay_) {
    auto root = tooltip_overlay_->GetParent().lock();
    if (root) {
      root->RemoveChild(tooltip_overlay_);
      root->Invalidate();
    }
    tooltip_overlay_.reset();
  }

  if (active_tooltip_.lock().get() == this) {
    active_tooltip_.reset();
  }
}

}  // namespace components
}  // namespace ui
}  // namespace perception
