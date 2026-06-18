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

#include <string>
#include <string_view>
#include <memory>

#include "perception/type_id.h"
#include "perception/ui/node.h"

namespace perception {
namespace ui {
namespace components {

class Tooltip : public UniqueIdentifiableType<Tooltip> {
 public:
  // Attaches a tooltip to an existing node.
  static void Attach(std::shared_ptr<Node> target_node, std::string_view text);

  // Modifier function to attach tooltips during Node construction.
  static auto ShowTooltip(std::string_view text) {
    return [text](Node& node) {
      Attach(node.ToSharedPtr(), text);
    };
  }

  Tooltip();

  void SetNode(std::weak_ptr<Node> node);
  void SetText(std::string_view text);
  std::string_view GetText() const;

 private:
  std::string text_;
  std::weak_ptr<Node> node_;
  std::shared_ptr<Node> tooltip_overlay_;

  void ShowTooltipAt(const Point& mouse_pos);
  void HideTooltip();
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::Tooltip>;

}  // namespace perception
