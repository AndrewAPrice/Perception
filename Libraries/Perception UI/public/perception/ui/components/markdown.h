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

#include <memory>
#include <string>
#include <string_view>

#include "perception/type_id.h"
#include "perception/ui/node.h"

namespace perception {
namespace ui {
namespace components {

// Parses Markdown text and populates a UI node hierarchy.
class Markdown : public UniqueIdentifiableType<Markdown> {
 public:
  // Creates a UI node containing rendered Markdown content.
  template <typename... Modifiers>
  static std::shared_ptr<Node> GenerateNode(std::string_view markdown_text,
                                            Modifiers... modifiers) {
    auto node = Node::Empty(
        [markdown_text](Markdown& markdown) {
          markdown.SetText(markdown_text);
        },
        modifiers...);
    return node;
  }

  Markdown();

  // Sets the node associated with this component.
  void SetNode(std::weak_ptr<Node> node);

  // Sets the markdown text to render.
  void SetText(std::string_view text);

  // Returns the current markdown text.
  std::string_view GetText() const;

 private:
  std::string text_;
  std::weak_ptr<Node> node_;

  // Rebuilds the UI node tree from markdown text.
  void Rebuild();
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::Markdown>;

}  // namespace perception
