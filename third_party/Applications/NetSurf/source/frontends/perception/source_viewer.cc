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

#include "source_viewer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "perception/clipboard.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/file_dialog.h"
#include "perception/ui/components/image_button.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/text_field.h"
#include "perception/ui/components/tooltip.h"
#include "perception/ui/font.h"
#include "perception/ui/image.h"
#include "perception/ui/layout.h"
#include "perception/ui/theme.h"
#include "tabs.h"

namespace {

// Path to the word wrap toggle icon asset.
constexpr std::string_view kWrapTextIconPath =
    "/Applications/NetSurf/wrap-text.svg";

// Path to the search icon asset.
constexpr std::string_view kSearchIconPath = "/Applications/NetSurf/search.svg";

// Path to the previous match chevron icon asset.
constexpr std::string_view kChevronUpIconPath =
    "/Applications/NetSurf/chevron-up.svg";

// Path to the next match chevron icon asset.
constexpr std::string_view kChevronDownIconPath =
    "/Applications/NetSurf/chevron-down.svg";

// Path to the copy icon asset.
constexpr std::string_view kCopyIconPath = "/Applications/NetSurf/copy.svg";

// Path to the save icon asset.
constexpr std::string_view kSaveIconPath = "/Applications/NetSurf/save.svg";

// Minimum character width for formatted line numbers.
constexpr size_t kMinLineNumberWidth = 4;

// Fallback line height in pixels when font metrics are unavailable.
constexpr float kDefaultLineHeight = 16.0f;

// Preferred width of the inline find input box.
constexpr float kFindInputWidth = 180.0f;

// Case-insensitive substring search helper.
bool ContainsCaseInsensitive(std::string_view haystack,
                             std::string_view needle) {
  if (needle.empty())
    return false;
  if (haystack.size() < needle.size())
    return false;
  auto it = std::search(
      haystack.begin(), haystack.end(), needle.begin(), needle.end(),
      [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
      });
  return it != haystack.end();
}

}  // namespace

namespace netsurf {
namespace perception {

using ::perception::ui::GetMonospace12UiFont;
using ::perception::ui::Image;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::ImageButton;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::ShowSaveFileDialog;
using ::perception::ui::components::TextField;
using ::perception::ui::components::Tooltip;

std::shared_ptr<SourceViewer> SourceViewer::Create(bool show_toolbar) {
  auto viewer = std::shared_ptr<SourceViewer>(new SourceViewer(show_toolbar));
  viewer->Initialize(show_toolbar);
  return viewer;
}

SourceViewer::SourceViewer(bool show_toolbar)
    : word_wrap_(false), find_bar_visible_(false), is_updating_text_(false) {
  (void)show_toolbar;
}

void SourceViewer::Initialize(bool show_toolbar) {
  std::weak_ptr<SourceViewer> weak_self = shared_from_this();

  text_field_node_ = TextField::BasicTextField(
      "", &text_field_,
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetWidthPercent(100.0f);
        layout.SetMinHeight(0.0f);
      },
      [weak_self](TextField& tf) {
        if (SkFont* mono = GetMonospace12UiFont())
          tf.SetFont(mono);
        tf.SetWordWrap(false);
        tf.OnTextChanged([weak_self](std::string_view current_text) {
          auto self = weak_self.lock();
          if (!self || self->is_updating_text_)
            return;
          if (current_text != self->displayed_text_) {
            self->is_updating_text_ = true;
            self->text_field_->SetText(self->displayed_text_);
            self->is_updating_text_ = false;
          }
        });
      });

  if (!show_toolbar) {
    root_node_ = Container::VerticalContainer(
        [](Layout& layout) {
          layout.SetFlexGrow(1.0f);
          layout.SetFlexShrink(1.0f);
          layout.SetWidthPercent(100.0f);
          layout.SetMinHeight(0.0f);
        },
        text_field_node_);
    return;
  }

  auto wrap_icon = Image::LoadImage(kWrapTextIconPath);
  auto search_icon = Image::LoadImage(kSearchIconPath);
  auto prev_icon = Image::LoadImage(kChevronUpIconPath);
  auto next_icon = Image::LoadImage(kChevronDownIconPath);
  auto copy_icon = Image::LoadImage(kCopyIconPath);
  auto save_icon = Image::LoadImage(kSaveIconPath);

  auto wrap_btn_node = ImageButton::BasicImageButton(
      [weak_self]() {
        if (auto self = weak_self.lock())
          self->SetWordWrap(!self->GetWordWrap());
      },
      wrap_icon, &wrap_button_, Tooltip::ShowTooltip("Wrap Lines"));

  auto find_btn_node = ImageButton::BasicImageButton(
      [weak_self]() {
        if (auto self = weak_self.lock())
          self->ToggleFindBar();
      },
      search_icon, &find_button_, Tooltip::ShowTooltip("Find in Source"));

  find_bar_node_ = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetAlignItems(YGAlignCenter);
        layout.SetDisplay(YGDisplayNone);
      },
      InputBox::BasicInputBox(
          "", &find_input_,
          [](Layout& layout) { layout.SetWidth(kFindInputWidth); },
          [weak_self](InputBox& input) {
            input.OnTextChanged([weak_self](std::string_view query) {
              if (auto self = weak_self.lock()) {
                self->find_query_ = std::string(query);
                self->UpdateFindMatches();
              }
            });
            input.OnEnterPressed([weak_self](std::string_view) {
              if (auto self = weak_self.lock())
                self->JumpToMatch(1);
            });
          }),
      ImageButton::BasicImageButton(
          [weak_self]() {
            if (auto self = weak_self.lock())
              self->JumpToMatch(-1);
          },
          prev_icon, Tooltip::ShowTooltip("Previous Match")),
      ImageButton::BasicImageButton(
          [weak_self]() {
            if (auto self = weak_self.lock())
              self->JumpToMatch(1);
          },
          next_icon, Tooltip::ShowTooltip("Next Match")),
      Label::SingleLineTruncated(
          "", &find_status_label_,
          [](Label& label) {
            label.SetColor(::perception::ui::kSecondaryTextColor);
          }));

  auto copy_btn_node = ImageButton::BasicImageButton(
      [weak_self]() {
        if (auto self = weak_self.lock())
          self->CopyAll();
      },
      copy_icon, Tooltip::ShowTooltip("Copy All"));

  auto save_btn_node = ImageButton::BasicImageButton(
      [weak_self]() {
        if (auto self = weak_self.lock())
          self->SaveAs();
      },
      save_icon, Tooltip::ShowTooltip("Save As..."));

  auto toolbar = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetFlexShrink(0.0f);
      },
      wrap_btn_node, find_btn_node, find_bar_node_,
      Node::Empty([](Layout& layout) { layout.SetFlexGrow(1.0f); }),
      copy_btn_node, save_btn_node);

  root_node_ = Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetWidthPercent(100.0f);
        layout.SetMinHeight(0.0f);
      },
      toolbar, text_field_node_);
}

std::shared_ptr<Node> SourceViewer::GetRootNode() const { return root_node_; }

void SourceViewer::SetContent(std::string_view text,
                              std::string_view suggested_filename) {
  raw_text_ = std::string(text);
  if (!suggested_filename.empty())
    suggested_filename_ = std::string(suggested_filename);
  RebuildDisplayedText();
  if (!find_query_.empty())
    UpdateFindMatches();
}

const std::string& SourceViewer::GetContent() const { return raw_text_; }

void SourceViewer::SetWordWrap(bool wrap) {
  word_wrap_ = wrap;
  if (wrap_button_)
    wrap_button_->SetToggled(wrap);
  if (text_field_)
    text_field_->SetWordWrap(wrap);
}

bool SourceViewer::GetWordWrap() const { return word_wrap_; }

void SourceViewer::ToggleFindBar() {
  find_bar_visible_ = !find_bar_visible_;
  if (find_button_)
    find_button_->SetToggled(find_bar_visible_);
  if (find_bar_node_) {
    find_bar_node_->GetLayout().SetDisplay(find_bar_visible_ ? YGDisplayFlex
                                                             : YGDisplayNone);
    if (root_node_)
      root_node_->Invalidate();
  }
}

void SourceViewer::CopyAll() const {
  ::perception::SetClipboard(std::string_view(raw_text_));
}

void SourceViewer::SaveAs() const {
  std::string content_copy = raw_text_;
  ShowSaveFileDialog(
      [content_copy](bool succeeded, std::string_view path) {
        if (!succeeded || path.empty())
          return;
        std::string path_str(path);
        FILE* file = fopen(path_str.c_str(), "wb");
        if (!file)
          return;
        if (!content_copy.empty())
          fwrite(content_copy.data(), 1, content_copy.size(), file);
        fclose(file);
      },
      {}, suggested_filename_, "", "Save Source As", GetGlobalUiWindow());
}

void SourceViewer::RebuildDisplayedText() {
  std::vector<std::string_view> lines;
  std::string_view sv(raw_text_);
  size_t pos = 0;
  while (pos <= sv.size()) {
    size_t nl = sv.find('\n', pos);
    if (nl == std::string_view::npos) {
      std::string_view line = sv.substr(pos);
      if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
      lines.push_back(line);
      break;
    }
    std::string_view line = sv.substr(pos, nl - pos);
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    lines.push_back(line);
    pos = nl + 1;
    if (pos == sv.size())
      break;
  }
  if (lines.empty())
    lines.push_back("");

  size_t num_digits = kMinLineNumberWidth;
  size_t n = lines.size();
  size_t digits = 0;
  while (n > 0) {
    digits++;
    n /= 10;
  }
  if (digits > num_digits)
    num_digits = digits;

  displayed_text_.clear();
  displayed_text_.reserve(raw_text_.size() + lines.size() * (num_digits + 4));
  char prefix_buf[32];
  for (size_t i = 0; i < lines.size(); ++i) {
    snprintf(prefix_buf, sizeof(prefix_buf), "%*zu | ",
             static_cast<int>(num_digits), i + 1);
    displayed_text_.append(prefix_buf);
    displayed_text_.append(lines[i]);
    if (i + 1 < lines.size())
      displayed_text_.push_back('\n');
  }

  if (text_field_) {
    is_updating_text_ = true;
    text_field_->SetText(displayed_text_);
    is_updating_text_ = false;
  }
  ScrollToLine(0);
}

void SourceViewer::UpdateFindMatches() {
  match_lines_.clear();
  current_match_index_ = 0;

  if (find_query_.empty()) {
    if (find_status_label_)
      find_status_label_->SetText("");
    return;
  }

  std::string_view sv(raw_text_);
  size_t pos = 0;
  size_t line_idx = 0;
  while (pos <= sv.size()) {
    size_t nl = sv.find('\n', pos);
    std::string_view line = (nl == std::string_view::npos)
                                ? sv.substr(pos)
                                : sv.substr(pos, nl - pos);
    if (ContainsCaseInsensitive(line, find_query_))
      match_lines_.push_back(line_idx);
    if (nl == std::string_view::npos)
      break;
    pos = nl + 1;
    line_idx++;
    if (pos == sv.size())
      break;
  }

  if (match_lines_.empty()) {
    if (find_status_label_)
      find_status_label_->SetText("No matches");
    return;
  }

  if (find_status_label_)
    find_status_label_->SetText("1 / " + std::to_string(match_lines_.size()));
  ScrollToLine(match_lines_[0]);
}

void SourceViewer::JumpToMatch(int delta) {
  if (match_lines_.empty())
    return;
  int count = static_cast<int>(match_lines_.size());
  int next = (static_cast<int>(current_match_index_) + delta) % count;
  if (next < 0)
    next += count;
  current_match_index_ = static_cast<size_t>(next);

  if (find_status_label_)
    find_status_label_->SetText(std::to_string(current_match_index_ + 1) +
                                " / " + std::to_string(match_lines_.size()));
  ScrollToLine(match_lines_[current_match_index_]);
}

void SourceViewer::ScrollToLine(size_t line_index) {
  if (!text_field_node_)
    return;
  auto scroll_container = text_field_node_->Get<ScrollContainer>();
  if (!scroll_container)
    return;

  float line_height = kDefaultLineHeight;
  if (text_field_ && text_field_->GetFont()) {
    SkFontMetrics metrics;
    float measured = text_field_->GetFont()->getMetrics(&metrics);
    if (measured > 0.0f)
      line_height = measured;
  }

  float target_y = static_cast<float>(line_index) * line_height;
  scroll_container->SetContentPosition({.x = 0.0f, .y = target_y});
  text_field_node_->Invalidate();
}

}  // namespace perception
}  // namespace netsurf
