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

#include "include/core/SkFont.h"
#include "perception/type_id.h"
#include "perception/ui/components/focusable.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/node.h"
#include "perception/ui/theme.h"
#include "perception/window/cursor.h"
#include "yoga/Yoga.h"

namespace perception {
namespace ui {
namespace components {

class TextField : public UniqueIdentifiableType<TextField> {
 public:
  // Creates a basic, multi-line vertically and horizontally scrollable text
  // field.
  template <typename... Modifiers>
  static std::shared_ptr<Node> BasicTextField(std::string_view text,
                                              Modifiers... modifiers) {
    std::shared_ptr<Node> inner_node = Node::Empty(
        [](Node& node) {
          node.SetBlocksHitTest(true);
          node.SetCursor(window::Cursor::Caret);
        },
        [](Layout& layout) {
          layout.SetMinHeightPercent(100.0f);
          layout.SetMinWidthPercent(100.0f);
        });
    auto node = ScrollContainer::BidirectionalScrollContainer(
        inner_node,
        [](Layout& layout) { layout.SetPadding(YGEdgeAll, kTextFieldPadding); },
        [text, inner_node](TextField& text_field) {
          text_field.SetInnerNode(inner_node);
          text_field.SetText(text);
        },
        modifiers...);

    return node;
  }

  TextField();

  void SetNode(std::weak_ptr<Node> node);
  void SetInnerNode(std::shared_ptr<Node> inner_node);

  void SetText(std::string_view text);
  std::string GetText() const;
  bool HasFocus() const;

  // Callback to use when the text has changed.
  void OnTextChanged(std::function<void(std::string_view)> handler);

  void SetFont(SkFont* font);
  SkFont* GetFont() const;

  void SetTextColor(uint32 color);
  uint32 GetTextColor() const;

  void SetWordWrap(bool word_wrap);
  bool GetWordWrap() const;

 private:
  struct LaidOutLine {
    size_t paragraph_index;
    size_t start_char;
    size_t end_char;
    float width;
  };

  std::weak_ptr<Node> node_;
  std::weak_ptr<Node> inner_node_;
  std::shared_ptr<Focusable> focusable_;
  SkFont* font_;
  std::vector<std::string> lines_;
  size_t cursor_line_;
  size_t cursor_char_;
  size_t selection_start_line_;
  size_t selection_start_char_;
  bool shift_pressed_;
  bool ctrl_pressed_;
  bool is_hovering_;
  bool is_pushed_;
  uint32 text_color_;
  bool word_wrap_;
  bool layout_is_dirty_;
  float last_layout_width_;
  std::vector<LaidOutLine> laid_out_lines_;

  std::vector<std::function<void(std::string_view)>> on_text_changed_handlers_;

  void DrawOuter(const DrawContext& draw_context);
  void DrawInner(const DrawContext& draw_context);
  Size MeasureInner(float width, YGMeasureMode width_mode, float height,
                    YGMeasureMode height_mode);

  void MoveCursorToPoint(const Point& point, bool extend_selection = false);
  void EnsureCursorVisible();
  void HandleKeyDown(const window::KeyboardKeyEvent& event);
  void HandleKeyUp(const window::KeyboardKeyEvent& event);

  void DeleteSelection();
  void InsertString(std::string_view str);
  std::string GetSelectedText() const;

  void NotifyTextChanged();
  void AssignDefaultFontIfUnassigned();
  void LayoutLinesIfNeeded(float max_width);
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::TextField>;

}  // namespace perception
