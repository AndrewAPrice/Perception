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
#include <vector>

#include "perception/type_id.h"
#include "perception/ui/node.h"
#include "perception/window/keyboard_key_event.h"

namespace perception {
namespace ui {
namespace components {

class Focusable : public UniqueIdentifiableType<Focusable> {
 public:
  Focusable();

  void SetNode(std::weak_ptr<Node> node);

  void OnFocus(std::function<void()> on_focus);
  void OnUnfocus(std::function<void()> on_unfocus);
  void OnKeyDown(
      std::function<void(const window::KeyboardKeyEvent&)> on_key_down);
  void OnKeyUp(std::function<void(const window::KeyboardKeyEvent&)> on_key_up);

  void Focus();
  void Unfocus();
  void KeyDown(const window::KeyboardKeyEvent& event);
  void KeyUp(const window::KeyboardKeyEvent& event);

  bool HasFocus() const;

 private:
  std::weak_ptr<Node> node_;
  std::vector<std::function<void()>> on_focus_handlers_;
  std::vector<std::function<void()>> on_unfocus_handlers_;
  std::vector<std::function<void(const window::KeyboardKeyEvent&)>>
      on_key_down_handlers_;
  std::vector<std::function<void(const window::KeyboardKeyEvent&)>>
      on_key_up_handlers_;
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::Focusable>;

}  // namespace perception
