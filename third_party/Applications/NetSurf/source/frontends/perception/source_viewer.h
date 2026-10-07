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

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "perception/ui/node.h"

namespace perception {
namespace ui {
namespace components {
class Button;
class InputBox;
class Label;
class TextField;
}  // namespace components
}  // namespace ui
}  // namespace perception

namespace netsurf {
namespace perception {

// Reusable read-only code and text viewer with line numbers, word-wrap toggle,
// inline search, clipboard copy, and file export.
class SourceViewer : public std::enable_shared_from_this<SourceViewer> {
 public:
  // Creates a new source viewer with an optional top action toolbar.
  static std::shared_ptr<SourceViewer> Create(bool show_toolbar = true);

  // Returns the root UI node of the viewer.
  std::shared_ptr<::perception::ui::Node> GetRootNode() const;

  // Sets the raw text content and suggested filename for saving.
  void SetContent(std::string_view text,
                  std::string_view suggested_filename = "source.txt");

  // Returns the raw text content without line number prefixes.
  const std::string& GetContent() const;

  // Sets whether long lines wrap within the viewport.
  void SetWordWrap(bool wrap);

  // Returns whether word wrap is enabled.
  bool GetWordWrap() const;

  // Toggles the visibility of the inline find bar.
  void ToggleFindBar();

  // Copies the raw text content to the clipboard.
  void CopyAll() const;

  // Opens a save file dialog to write the raw text content to disk.
  void SaveAs() const;

 private:
  explicit SourceViewer(bool show_toolbar);

  // Builds the UI node hierarchy for the source viewer.
  void Initialize(bool show_toolbar);

  // Formats the raw text with 1-based line numbers and updates the text field.
  void RebuildDisplayedText();

  // Recomputes line matches for the active search query.
  void UpdateFindMatches();

  // Advances or retreats the active search match by delta and scrolls to it.
  void JumpToMatch(int delta);

  // Scrolls the text field viewport to bring the given 0-based line into view.
  void ScrollToLine(size_t line_index);

  std::shared_ptr<::perception::ui::Node> root_node_;
  std::shared_ptr<::perception::ui::Node> text_field_node_;
  std::shared_ptr<::perception::ui::components::TextField> text_field_;

  std::shared_ptr<::perception::ui::components::Button> wrap_button_;
  std::shared_ptr<::perception::ui::components::Button> find_button_;
  std::shared_ptr<::perception::ui::Node> find_bar_node_;
  std::shared_ptr<::perception::ui::components::InputBox> find_input_;
  std::shared_ptr<::perception::ui::components::Label> find_status_label_;

  std::string raw_text_;
  std::string displayed_text_;
  std::string suggested_filename_ = "source.txt";
  std::string find_query_;
  std::vector<size_t> match_lines_;
  size_t current_match_index_ = 0;

  bool word_wrap_ = false;
  bool find_bar_visible_ = false;
  bool is_updating_text_ = false;
};

}  // namespace perception
}  // namespace netsurf
