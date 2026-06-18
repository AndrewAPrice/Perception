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

#include "perception/ui/components/pop_up.h"

#include <algorithm>
#include <cmath>

#include "perception/ui/components/scroll_bar.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/window/mouse_button.h"

namespace perception {
template class UniqueIdentifiableType<ui::components::PopUp>;

namespace ui {
namespace components {

PopUp::PopUp() : focus_notify_id_(0), is_closed_(false) {}

PopUp::~PopUp() {
  if (!window_.expired() && focus_notify_id_ != 0) {
    window_.lock()->StopNotifyingOnFocusChanged(focus_notify_id_);
  }
}

void PopUp::SetNode(std::weak_ptr<Node> node) { node_ = node; }

std::shared_ptr<PopUp> PopUp::Show(std::shared_ptr<Node> context_node,
                                   Point anchor,
                                   std::shared_ptr<Node> content,
                                   std::function<void()> on_close) {
  if (!context_node || !content) return nullptr;

  // 1. Walk up to root node
  std::shared_ptr<Node> root = context_node;
  while (true) {
    auto parent = root->GetParent().lock();
    if (!parent) break;
    root = parent;
  }

  // 2. Measure root window dimensions
  float window_w = root->GetLayout().GetCalculatedWidth();
  float window_h = root->GetLayout().GetCalculatedHeight();
  if (window_w <= 0.0f || window_h <= 0.0f) {
    auto sz = root->GetSize();
    window_w = sz.width;
    window_h = sz.height;
  }
  if (window_w <= 0.0f) window_w = 800.0f;
  if (window_h <= 0.0f) window_h = 600.0f;

  // 3. Measure content dimensions
  YGValue target_w_val = content->GetLayout().GetWidth();
  float target_w =
      (target_w_val.unit == YGUnitPoint) ? target_w_val.value : YGUndefined;
  content->GetLayout().Calculate(target_w, YGUndefined);

  float content_w = content->GetLayout().GetCalculatedWidthWithMargin();
  float content_h = content->GetLayout().GetCalculatedHeightWithMargin();

  // 4. Adjust anchor coordinates
  float x = anchor.x;
  float y = anchor.y;

  if (x + content_w > window_w) {
    x = std::max(0.0f, window_w - content_w);
  }

  float top = y;
  bool use_scroll = false;
  float max_h = window_h;

  if (y + content_h <= window_h) {
    top = y;
  } else if (content_h <= window_h) {
    top = std::max(0.0f, window_h - content_h);
  } else {
    top = 0.0f;
    use_scroll = true;
    max_h = window_h;
  }

  auto popup_obj = std::make_shared<PopUp>();
  popup_obj->on_close_ = on_close;

  auto popup_container = std::make_shared<Node>();
  popup_container->GetLayout().SetPositionType(YGPositionTypeAbsolute);
  popup_container->GetLayout().SetWidthPercent(100.f);
  popup_container->GetLayout().SetHeightPercent(100.f);
  popup_container->GetLayout().SetPosition(YGEdgeLeft, 0.f);
  popup_container->GetLayout().SetPosition(YGEdgeTop, 0.f);
  popup_container->SetBlocksHitTest(true);

  popup_container->Add<PopUp>(popup_obj);
  popup_obj->node_ = popup_container;

  std::weak_ptr<PopUp> weak_popup = popup_obj;

  auto overlay = std::make_shared<Node>();
  overlay->GetLayout().SetPositionType(YGPositionTypeAbsolute);
  overlay->GetLayout().SetWidthPercent(100.f);
  overlay->GetLayout().SetHeightPercent(100.f);
  overlay->GetLayout().SetPosition(YGEdgeLeft, 0.f);
  overlay->GetLayout().SetPosition(YGEdgeTop, 0.f);
  overlay->SetBlocksHitTest(true);

  overlay->OnMouseButtonDown(
      [weak_popup](const Point&, window::MouseButton) {
        if (!weak_popup.expired()) {
          weak_popup.lock()->Close();
        }
      });

  popup_container->AddChild(overlay);

  if (use_scroll) {
    auto scroll_container = ScrollContainer::VerticalScrollContainer(
        content, [](Layout& l) { l.SetPositionType(YGPositionTypeAbsolute); });
    scroll_container->GetLayout().SetPosition(YGEdgeLeft, x);
    scroll_container->GetLayout().SetPosition(YGEdgeTop, top);
    scroll_container->GetLayout().SetWidth(content_w + ScrollBar::kWidth);
    scroll_container->GetLayout().SetHeight(max_h);
    popup_container->AddChild(scroll_container);
  } else {
    content->GetLayout().SetPositionType(YGPositionTypeAbsolute);
    content->GetLayout().SetPosition(YGEdgeLeft, x);
    content->GetLayout().SetPosition(YGEdgeTop, top);
    popup_container->AddChild(content);
  }

  if (auto ui_window = root->Get<UiWindow>()) {
    popup_obj->window_ = ui_window;
    popup_obj->focus_notify_id_ =
        ui_window->NotifyOnFocusChanged([weak_popup, ui_window]() {
          if (!ui_window->IsFocused() && !weak_popup.expired()) {
            weak_popup.lock()->Close();
          }
        });
  }

  root->AddChild(popup_container);
  root->Invalidate();

  return popup_obj;
}

void PopUp::Close(std::shared_ptr<Node> node) {
  if (!node) return;
  auto curr = node;
  while (curr) {
    if (auto popup = curr->Get<PopUp>()) {
      popup->Close();
      return;
    }
    curr = curr->GetParent().lock();
  }
}

void PopUp::Close() {
  if (is_closed_) return;
  is_closed_ = true;

  if (!window_.expired() && focus_notify_id_ != 0) {
    window_.lock()->StopNotifyingOnFocusChanged(focus_notify_id_);
    focus_notify_id_ = 0;
  }

  if (!node_.expired()) {
    auto popup_node = node_.lock();
    if (auto parent = popup_node->GetParent().lock()) {
      parent->RemoveChild(popup_node);
      parent->Invalidate();
    }
  }

  if (on_close_) {
    on_close_();
  }
}

}  // namespace components
}  // namespace ui
}  // namespace perception
