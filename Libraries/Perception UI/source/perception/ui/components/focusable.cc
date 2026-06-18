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

#include "perception/ui/components/focusable.h"

#include "perception/ui/components/ui_window.h"

namespace perception {
template class UniqueIdentifiableType<ui::components::Focusable>;

namespace ui {
namespace components {
namespace {

std::shared_ptr<UiWindow> FindUiWindowOfNode(std::shared_ptr<Node> node) {
  while (node) {
    if (auto ui_window = node->Get<UiWindow>()) return ui_window;
    node = node->GetParent().lock();
  }
  return nullptr;
}

}  // namespace

Focusable::Focusable() {}

void Focusable::SetNode(std::weak_ptr<Node> node) { node_ = node; }

void Focusable::OnFocus(std::function<void()> on_focus) {
  on_focus_handlers_.push_back(on_focus);
}

void Focusable::OnUnfocus(std::function<void()> on_unfocus) {
  on_unfocus_handlers_.push_back(on_unfocus);
}

void Focusable::OnKeyDown(
    std::function<void(const window::KeyboardKeyEvent&)> on_key_down) {
  on_key_down_handlers_.push_back(on_key_down);
}

void Focusable::OnKeyUp(
    std::function<void(const window::KeyboardKeyEvent&)> on_key_up) {
  on_key_up_handlers_.push_back(on_key_up);
}

void Focusable::Focus() {
  if (auto node = node_.lock()) {
    node->Invalidate();
    if (auto ui_window = FindUiWindowOfNode(node)) {
      auto old_focused = ui_window->GetFocusedNode();
      if (old_focused && old_focused != node) {
        if (auto focusable = old_focused->Get<Focusable>())
          focusable->Unfocus();
      }
      ui_window->SetFocusedNode(node);
    }
  }

  for (auto& handler : on_focus_handlers_) handler();
}

void Focusable::Unfocus() {
  if (auto node = node_.lock()) {
    node->Invalidate();
    if (auto ui_window = FindUiWindowOfNode(node)) {
      if (ui_window->GetFocusedNode() == node)
        ui_window->SetFocusedNode(nullptr);
    }
  }

  for (auto& handler : on_unfocus_handlers_) handler();
}

void Focusable::KeyDown(const window::KeyboardKeyEvent& event) {
  for (auto& handler : on_key_down_handlers_) handler(event);
}

void Focusable::KeyUp(const window::KeyboardKeyEvent& event) {
  for (auto& handler : on_key_up_handlers_) handler(event);
}

bool Focusable::HasFocus() const {
  if (auto node = node_.lock()) {
    if (auto ui_window = FindUiWindowOfNode(node))
      return ui_window->GetFocusedNode() == node;
  }
  return false;
}

}  // namespace components
}  // namespace ui
}  // namespace perception
