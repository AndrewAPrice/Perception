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

#include "perception/ui/components/markdown.h"

#include <cctype>
#include <memory>
#include <vector>

#include "perception/loader.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "perception/ui/theme.h"
#include "perception/window/mouse_button.h"

namespace perception {

template class UniqueIdentifiableType<ui::components::Markdown>;

namespace ui {
namespace components {
namespace {

// Default gap between block elements in a Markdown layout.
constexpr float kBlockGap = 6.0f;

// Font size for H1 headers.
constexpr float kH1FontSize = 18.0f;

// Font size for H2 headers.
constexpr float kH2FontSize = 15.0f;

// Font size for H3 headers.
constexpr float kH3FontSize = 13.0f;

// Font size for body text and H4-H6 headers.
constexpr float kBodyFontSize = 12.0f;

// Font size for code blocks and inline code spans.
constexpr float kCodeFontSize = 11.0f;

// Approximate width of a space character in standard UI font.
constexpr float kSpaceWidth = 4.0f;

// Left padding for blockquotes.
constexpr float kBlockquotePaddingLeft = 10.0f;

// Left border width for blockquotes.
constexpr float kBlockquoteBorderLeftWidth = 3.0f;

// Padding inside code blocks.
constexpr float kCodeBlockPadding = 8.0f;

// Corner radius for code blocks.
constexpr float kCodeBlockCornerRadius = 4.0f;

// Background color for code blocks.
constexpr uint32_t kCodeBlockBackgroundColor = 0xFF2A2D32;

// Text color for code blocks.
constexpr uint32_t kCodeBlockTextColor = 0xFFE6EDF3;

// Background color for inline code badges.
constexpr uint32_t kInlineCodeBackgroundColor = 0xFFE2E8F0;

// Text color for inline code badges.
constexpr uint32_t kInlineCodeTextColor = 0xFF0F172A;

// Text color for clickable links.
constexpr uint32_t kLinkTextColor = 0xFF2563EB;

// Horizontal rule height.
constexpr float kHorizontalRuleHeight = 1.0f;

// Structure describing an inline text span.
struct TextSpan {
  std::string text;
  bool bold = false;
  bool italic = false;
  bool code = false;
  std::string link_url;
  int leading_spaces = 0;
  int trailing_spaces = 0;
};

// Helper function to parse inline markdown formatting into text spans.
std::vector<TextSpan> ParseInlineSpans(std::string_view line) {
  std::vector<TextSpan> spans;
  std::string current_text;
  bool bold = false;
  bool italic = false;
  bool code = false;

  auto flush_current_text = [&]() {
    if (current_text.empty()) return;

    int leading = 0;
    while (leading < static_cast<int>(current_text.length()) &&
           current_text[leading] == ' ')
      leading++;

    if (leading == static_cast<int>(current_text.length())) {
      if (!spans.empty()) spans.back().trailing_spaces += leading;
      current_text.clear();
      return;
    }

    int trailing = 0;
    while (trailing < static_cast<int>(current_text.length()) &&
           current_text[current_text.length() - 1 - trailing] == ' ')
      trailing++;

    std::string trimmed = current_text.substr(
        leading, current_text.length() - leading - trailing);
    spans.push_back({trimmed, bold, italic, code, "", leading, trailing});
    current_text.clear();
  };

  size_t i = 0;
  while (i < line.length()) {
    if (!code && line[i] == '[') {
      size_t close_bracket = line.find(']', i + 1);
      if (close_bracket != std::string_view::npos &&
          close_bracket + 1 < line.length() && line[close_bracket + 1] == '(') {
        size_t close_paren = line.find(')', close_bracket + 2);
        if (close_paren != std::string_view::npos) {
          flush_current_text();
          std::string_view link_text =
              line.substr(i + 1, close_bracket - i - 1);
          std::string_view link_url =
              line.substr(close_bracket + 2, close_paren - close_bracket - 2);

          int leading = 0;
          while (leading < static_cast<int>(link_text.length()) &&
                 link_text[leading] == ' ')
            leading++;

          int trailing = 0;
          while (trailing < static_cast<int>(link_text.length()) &&
                 link_text[link_text.length() - 1 - trailing] == ' ')
            trailing++;

          std::string trimmed = std::string(link_text.substr(
              leading, link_text.length() - leading - trailing));
          spans.push_back({trimmed, bold, italic, false, std::string(link_url),
                           leading, trailing});
          i = close_paren + 1;
          continue;
        }
      }
    }

    if (!code && (line.substr(i, 2) == "**" || line.substr(i, 2) == "__")) {
      flush_current_text();
      bold = !bold;
      i += 2;
    } else if (!code && (line[i] == '*' || line[i] == '_')) {
      flush_current_text();
      italic = !italic;
      i += 1;
    } else if (line[i] == '`') {
      flush_current_text();
      code = !code;
      i += 1;
    } else {
      current_text.push_back(line[i]);
      i += 1;
    }
  }

  flush_current_text();
  return spans;
}

// Helper to construct UI nodes for a line with inline formatting.
std::shared_ptr<Node> CreateLineNode(std::string_view line_text) {
  auto spans = ParseInlineSpans(line_text);
  if (spans.empty()) return nullptr;

  if (spans.size() == 1 && !spans[0].bold && !spans[0].italic &&
      !spans[0].code && spans[0].link_url.empty() &&
      spans[0].leading_spaces == 0 && spans[0].trailing_spaces == 0)
    return Label::BasicLabel(spans[0].text);

  auto line_container = Node::Empty([](Layout& layout) {
    layout.SetFlexDirection(YGFlexDirectionRow);
    layout.SetFlexWrap(YGWrapWrap);
    layout.SetAlignItems(YGAlignBaseline);
  });

  for (const auto& span : spans) {
    if (span.text.empty()) continue;

    float margin_left = span.leading_spaces * kSpaceWidth;
    float margin_right = span.trailing_spaces * kSpaceWidth;

    if (!span.link_url.empty()) {
      std::string link_url = span.link_url;
      auto link_label = Label::BasicLabel(
          span.text,
          [span](Label& label) {
            label.SetFont(
                GetUiFont("DejaVuSans", kBodyFontSize, span.bold, span.italic));
            label.SetColor(kLinkTextColor);
          },
          [margin_left, margin_right](Layout& layout) {
            if (margin_left > 0.0f) layout.SetMargin(YGEdgeLeft, margin_left);
            if (margin_right > 0.0f)
              layout.SetMargin(YGEdgeRight, margin_right);
          });

      link_label->OnMouseButtonUp(
          [link_url](const Point& point, window::MouseButton button) {
            if (button != window::MouseButton::Left) return;
            perception::LoadApplicationRequest request;
            request.name = link_url;
            perception::GetService<perception::Loader>().LaunchApplication(
                request, nullptr);
          });

      link_label->SetCursor(window::Cursor::Poke);
      line_container->AddChild(link_label);
    } else if (span.code) {
      margin_left += 2.0f;
      margin_right += 2.0f;

      line_container->AddChild(Node::Empty(
          [span](Block& block) {
            block.SetFillColor(kInlineCodeBackgroundColor);
            block.SetBorderRadius(3.0f);
          },
          [margin_left, margin_right](Layout& layout) {
            layout.SetPadding(YGEdgeLeft, 4.0f);
            layout.SetPadding(YGEdgeRight, 4.0f);
            layout.SetPadding(YGEdgeTop, 1.0f);
            layout.SetPadding(YGEdgeBottom, 1.0f);
            if (margin_left > 0.0f) layout.SetMargin(YGEdgeLeft, margin_left);
            if (margin_right > 0.0f)
              layout.SetMargin(YGEdgeRight, margin_right);
          },
          Label::BasicLabel(span.text, [span](Label& label) {
            label.SetFont(GetUiFont("DejaVuSansMono", kCodeFontSize, span.bold,
                                    span.italic));
            label.SetColor(kInlineCodeTextColor);
          })));
    } else {
      line_container->AddChild(Label::BasicLabel(
          span.text,
          [span](Label& label) {
            label.SetFont(
                GetUiFont("DejaVuSans", kBodyFontSize, span.bold, span.italic));
          },
          [margin_left, margin_right](Layout& layout) {
            if (margin_left > 0.0f) layout.SetMargin(YGEdgeLeft, margin_left);
            if (margin_right > 0.0f)
              layout.SetMargin(YGEdgeRight, margin_right);
          }));
    }
  }

  return line_container;
}

}  // namespace

Markdown::Markdown() = default;

void Markdown::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  Rebuild();
}

void Markdown::SetText(std::string_view text) {
  if (text_ == text) return;
  text_ = text;
  Rebuild();
}

std::string_view Markdown::GetText() const { return text_; }

void Markdown::Rebuild() {
  if (node_.expired()) return;
  auto strong_node = node_.lock();
  strong_node->RemoveChildren();

  auto layout = strong_node->GetLayout();
  layout.SetFlexDirection(YGFlexDirectionColumn);
  layout.SetGap(kBlockGap);

  std::vector<std::string_view> lines;
  size_t start = 0;
  while (true) {
    size_t pos = text_.find('\n', start);
    if (pos == std::string::npos) {
      lines.push_back(std::string_view(text_).substr(start));
      break;
    }
    lines.push_back(std::string_view(text_).substr(start, pos - start));
    start = pos + 1;
  }

  size_t line_index = 0;
  while (line_index < lines.size()) {
    std::string_view line = lines[line_index];

    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

    if (line.empty()) {
      line_index++;
      continue;
    }

    if (line.substr(0, 3) == "```") {
      line_index++;
      std::vector<std::string> code_lines;
      while (line_index < lines.size() &&
             lines[line_index].substr(0, 3) != "```") {
        std::string_view code_line = lines[line_index];
        if (!code_line.empty() && code_line.back() == '\r')
          code_line.remove_suffix(1);
        code_lines.push_back(std::string(code_line));
        line_index++;
      }
      if (line_index < lines.size()) line_index++;

      auto code_container = Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetPadding(YGEdgeAll, kCodeBlockPadding);
            layout.SetGap(2.0f);
          },
          [](Block& block) {
            block.SetFillColor(kCodeBlockBackgroundColor);
            block.SetBorderRadius(kCodeBlockCornerRadius);
          });

      for (const auto& code_l : code_lines) {
        code_container->AddChild(Label::BasicLabel(code_l, [](Label& label) {
          label.SetFont(GetUiFont("DejaVuSansMono", kCodeFontSize));
          label.SetColor(kCodeBlockTextColor);
        }));
      }

      strong_node->AddChild(code_container);
      continue;
    }

    if (line == "---" || line == "***") {
      strong_node->AddChild(Node::Empty(
          [](Block& block) { block.SetFillColor(kGroupBoxBorderColor); },
          [](Layout& layout) {
            layout.SetHeight(kHorizontalRuleHeight);
            layout.SetWidthPercent(100.0f);
            layout.SetMargin(YGEdgeTop, kBlockGap);
            layout.SetMargin(YGEdgeBottom, kBlockGap);
          }));
      line_index++;
      continue;
    }

    if (line[0] == '#') {
      size_t level = 0;
      while (level < line.length() && line[level] == '#') level++;
      if (level <= 6 && level < line.length() && line[level] == ' ') {
        std::string_view header_text = line.substr(level + 1);
        float font_size = kBodyFontSize;
        if (level == 1)
          font_size = kH1FontSize;
        else if (level == 2)
          font_size = kH2FontSize;
        else if (level == 3)
          font_size = kH3FontSize;

        strong_node->AddChild(Label::BasicLabel(
            header_text,
            [font_size](Label& label) {
              label.SetFont(GetUiFont("DejaVuSans", font_size, true));
            },
            [](Layout& layout) {
              layout.SetMargin(YGEdgeTop, kBlockGap);
              layout.SetMargin(YGEdgeBottom, 2.0f);
            }));
        line_index++;
        continue;
      }
    }

    if (line[0] == '>' && line.length() > 1) {
      std::string_view quote_text = line.substr(1);
      if (quote_text[0] == ' ') quote_text.remove_prefix(1);

      strong_node->AddChild(Node::Empty(
          [](Block& block) {
            block.SetBorderColor(kGroupBoxBorderColor);
            block.SetBorderWidth(kBlockquoteBorderLeftWidth);
          },
          [](Layout& layout) {
            layout.SetPadding(YGEdgeLeft, kBlockquotePaddingLeft);
          },
          CreateLineNode(quote_text)));
      line_index++;
      continue;
    }

    if ((line.substr(0, 2) == "* " || line.substr(0, 2) == "- " ||
         line.substr(0, 2) == "+ " || line.substr(0, 3) == "• ") ||
        (line.length() > 2 &&
         (line[0] == '*' || line[0] == '-' || line[0] == '+') &&
         line[1] == ' ')) {
      size_t prefix_len = (line.substr(0, 3) == "• ") ? 3 : 2;
      std::string_view item_text = line.substr(prefix_len);

      auto row = Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetAlignItems(YGAlignBaseline);
            layout.SetGap(4.0f);
          },
          Label::BasicLabel("•"), CreateLineNode(item_text));
      strong_node->AddChild(row);
      line_index++;
      continue;
    }

    size_t dot_pos = line.find(". ");
    if (dot_pos != std::string::npos && dot_pos > 0 && dot_pos <= 3) {
      bool all_digits = true;
      for (size_t d = 0; d < dot_pos; ++d) {
        if (!std::isdigit(line[d])) {
          all_digits = false;
          break;
        }
      }
      if (all_digits) {
        std::string_view num_prefix = line.substr(0, dot_pos + 1);
        std::string_view item_text = line.substr(dot_pos + 2);

        auto row = Container::HorizontalContainer(
            [](Layout& layout) {
              layout.SetAlignItems(YGAlignBaseline);
              layout.SetGap(4.0f);
            },
            Label::BasicLabel(num_prefix), CreateLineNode(item_text));
        strong_node->AddChild(row);
        line_index++;
        continue;
      }
    }

    auto paragraph_node = CreateLineNode(line);
    if (paragraph_node) strong_node->AddChild(paragraph_node);
    line_index++;
  }
}

}  // namespace components
}  // namespace ui
}  // namespace perception
