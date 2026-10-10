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

#include "terminal_buffer.h"

#include <algorithm>
#include <cmath>

namespace {

// Standard tab stop interval in columns.
constexpr int kTabStopWidth = 8;

// Default 16-color ANSI palette for the dark theme (Catppuccin Mocha).
constexpr std::array<uint32, 16> kInitialAnsiColors = {
    0xFF181825,  // Black
    0xFFF38BA8,  // Red
    0xFFA6E3A1,  // Green
    0xFFF9E2AF,  // Yellow
    0xFF89B4FA,  // Blue
    0xFFCBA6F7,  // Magenta
    0xFF94E2D5,  // Cyan
    0xFFBAC2DE,  // White
    0xFF585B70,  // Bright Black
    0xFFF38BA8,  // Bright Red
    0xFFA6E3A1,  // Bright Green
    0xFFF9E2AF,  // Bright Yellow
    0xFF89B4FA,  // Bright Blue
    0xFFF5C2E7,  // Bright Magenta
    0xFF89DCEB,  // Bright Cyan
    0xFFA6ADC8,  // Bright White
};

// Default 16-color ANSI palette for the light theme (Catppuccin Latte).
constexpr std::array<uint32, 16> kLightAnsiColors = {
    0xFF5C5F77,  // Black
    0xFFD20F39,  // Red
    0xFF40A02B,  // Green
    0xFFDF8E1D,  // Yellow
    0xFF1E66F5,  // Blue
    0xFF8839EF,  // Magenta
    0xFF179299,  // Cyan
    0xFFACB0BE,  // White
    0xFF6C6F85,  // Bright Black
    0xFFD20F39,  // Bright Red
    0xFF40A02B,  // Bright Green
    0xFFDF8E1D,  // Bright Yellow
    0xFF1E66F5,  // Bright Blue
    0xFFEA76CB,  // Bright Magenta
    0xFF04A5E5,  // Bright Cyan
    0xFFBCC0CC,  // Bright White
};

// Component levels for the 6x6x6 256-color RGB cube.
constexpr std::array<uint8, 6> kCubeLevels = {0x00, 0x5F, 0x87,
                                              0xAF, 0xD7, 0xFF};

// First index of the 6x6x6 RGB cube in the 256-color palette.
constexpr int kColorCubeStart = 16;

// First index of the 24-step grayscale ramp in the 256-color palette.
constexpr int kGrayscaleRampStart = 232;

// Base intensity value for the 256-color grayscale ramp.
constexpr int kGrayscaleRampBase = 8;

// Intensity step per entry in the 256-color grayscale ramp.
constexpr int kGrayscaleRampStep = 10;

// Maximum scale factor for Kitty OSC 66 multi-cell text.
constexpr int kMaxOsc66Scale = 7;

// Remaps a cell or pen color from the previous theme palette to the new theme palette.
uint32 RemapThemeColor(uint32 color, uint32 old_fg, uint32 old_bg,
                       const std::array<uint32, 16>& old_ansi, uint32 new_fg,
                       uint32 new_bg, const std::array<uint32, 16>& new_ansi) {
  if (color == old_fg)
    return new_fg;
  if (color == old_bg)
    return new_bg;
  for (size_t i = 0; i < old_ansi.size(); ++i) {
    if (color == old_ansi[i])
      return new_ansi[i];
  }
  return color;
}

// Appends a UTF-32 codepoint encoded as UTF-8 into `out`.
void AppendCodepointUtf8(char32_t cp, std::string& out) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0x10FFFF) {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

// Decodes the next UTF-8 codepoint from `sv` starting at `index`.
char32_t NextUtf8Codepoint(std::string_view sv, size_t& index) {
  if (index >= sv.size())
    return 0;
  uint8 b0 = static_cast<uint8>(sv[index++]);
  if ((b0 & 0x80) == 0)
    return b0;
  if ((b0 & 0xE0) == 0xC0 && index < sv.size()) {
    uint8 b1 = static_cast<uint8>(sv[index++]) & 0x3F;
    return ((b0 & 0x1F) << 6) | b1;
  }
  if ((b0 & 0xF0) == 0xE0 && index + 1 < sv.size()) {
    uint8 b1 = static_cast<uint8>(sv[index++]) & 0x3F;
    uint8 b2 = static_cast<uint8>(sv[index++]) & 0x3F;
    return ((b0 & 0x0F) << 12) | (b1 << 6) | b2;
  }
  if ((b0 & 0xF8) == 0xF0 && index + 2 < sv.size()) {
    uint8 b1 = static_cast<uint8>(sv[index++]) & 0x3F;
    uint8 b2 = static_cast<uint8>(sv[index++]) & 0x3F;
    uint8 b3 = static_cast<uint8>(sv[index++]) & 0x3F;
    return ((b0 & 0x07) << 18) | (b1 << 12) | (b2 << 6) | b3;
  }
  return U'\uFFFD';
}

// Maps ASCII characters in DEC Special Graphics mode ('0') to Unicode box drawing.
char32_t MapDecSpecialGraphics(char32_t cp) {
  switch (cp) {
    case U'`':
      return U'\u25C6';  // Diamond
    case U'a':
      return U'\u2592';  // Checkerboard
    case U'f':
      return U'\u00B0';  // Degree symbol
    case U'g':
      return U'\u00B1';  // Plus/minus
    case U'j':
      return U'\u2518';  // Lower-right corner
    case U'k':
      return U'\u2510';  // Upper-right corner
    case U'l':
      return U'\u250C';  // Upper-left corner
    case U'm':
      return U'\u2514';  // Lower-left corner
    case U'n':
      return U'\u253C';  // Crossing lines
    case U'q':
      return U'\u2500';  // Horizontal line
    case U't':
      return U'\u251C';  // Left tee
    case U'u':
      return U'\u2524';  // Right tee
    case U'v':
      return U'\u2534';  // Bottom tee
    case U'w':
      return U'\u252C';  // Top tee
    case U'x':
      return U'\u2502';  // Vertical line
    case U'y':
      return U'\u2264';  // Less than or equal
    case U'z':
      return U'\u2265';  // Greater than or equal
    case U'{':
      return U'\u03C0';  // Pi
    case U'|':
      return U'\u2260';  // Not equal
    case U'}':
      return U'\u00A3';  // UK pound
    case U'~':
      return U'\u00B7';  // Centered dot
    default:
      return cp;
  }
}

// Returns true if a cell is a default unstyled space.
bool IsDefaultBlankCell(const Cell& cell, uint32 default_fg,
                        uint32 default_bg) {
  return cell.codepoint == U' ' && cell.bg_color == default_bg &&
         cell.fg_color == default_fg && cell.underline_style == 0 &&
         cell.flags == 0 && cell.scale == 1 && cell.hyperlink_id == 0;
}

}  // namespace

TerminalBuffer::TerminalBuffer(int rows, int cols)
    : rows_(std::max(1, rows)),
      cols_(std::max(1, cols)),
      cursor_row_(0),
      cursor_col_(0),
      wrap_pending_(false),
      scroll_top_(0),
      scroll_bottom_(rows_ - 1),
      base_line_index_(0),
      scroll_offset_lines_(0),
      using_alt_screen_(false),
      active_theme_(TerminalTheme::Dark),
      theme_default_fg_(kDefaultTerminalForegroundColor),
      theme_default_bg_(kDefaultTerminalBackgroundColor),
      theme_default_cursor_(kDefaultTerminalCursorColor),
      selection_highlight_color_(kTerminalSelectionHighlightColor),
      cursor_visible_(true),
      cursor_blink_(true),
      cursor_style_(0),
      auto_wrap_mode_(true),
      origin_mode_(false),
      application_cursor_keys_(false),
      bracketed_paste_mode_(false),
      synchronized_output_(false),
      in_band_resize_mode_(false),
      focus_reporting_mode_(false),
      mouse_tracking_mode_(0),
      mouse_encoding_mode_(0),
      dec_special_graphics_(false),
      min_dirty_row_(0),
      max_dirty_row_(rows_ - 1),
      full_dirty_(true) {
  InitializeDefaultTheme();
  Reset();
}

void TerminalBuffer::InitializeDefaultTheme() {
  if (active_theme_ == TerminalTheme::Light) {
    theme_default_fg_ = kLightTerminalForegroundColor;
    theme_default_bg_ = kLightTerminalBackgroundColor;
    theme_default_cursor_ = kLightTerminalCursorColor;
    selection_highlight_color_ = kLightTerminalSelectionHighlightColor;
    theme_.ansi_colors = kLightAnsiColors;
  } else {
    theme_default_fg_ = kDefaultTerminalForegroundColor;
    theme_default_bg_ = kDefaultTerminalBackgroundColor;
    theme_default_cursor_ = kDefaultTerminalCursorColor;
    selection_highlight_color_ = kTerminalSelectionHighlightColor;
    theme_.ansi_colors = kInitialAnsiColors;
  }
  theme_.default_fg = theme_default_fg_;
  theme_.default_bg = theme_default_bg_;
  theme_.cursor_color = theme_default_cursor_;
}

void TerminalBuffer::ApplyTheme(TerminalTheme theme) {
  uint32 old_fg = theme_.default_fg;
  uint32 old_bg = theme_.default_bg;
  uint32 old_cursor = theme_.cursor_color;
  std::array<uint32, 16> old_ansi = theme_.ansi_colors;

  active_theme_ = theme;
  InitializeDefaultTheme();

  uint32 new_fg = theme_.default_fg;
  uint32 new_bg = theme_.default_bg;
  const auto& new_ansi = theme_.ansi_colors;

  auto remap_cell = [&](Cell& cell) {
    cell.fg_color = RemapThemeColor(cell.fg_color, old_fg, old_bg, old_ansi,
                                    new_fg, new_bg, new_ansi);
    cell.bg_color = RemapThemeColor(cell.bg_color, old_fg, old_bg, old_ansi,
                                    new_fg, new_bg, new_ansi);
  };

  remap_cell(current_pen_);
  remap_cell(saved_primary_cursor_.pen);
  remap_cell(saved_alt_cursor_.pen);
  remap_cell(alt_screen_saved_cursor_.pen);

  for (auto& line : primary_screen_) {
    for (auto& cell : line.cells)
      remap_cell(cell);
  }
  for (auto& line : alt_screen_) {
    for (auto& cell : line.cells)
      remap_cell(cell);
  }
  for (auto& line : scrollback_) {
    for (auto& cell : line.cells)
      remap_cell(cell);
  }
  for (auto& line : sync_snapshot_lines_) {
    for (auto& cell : line.cells)
      remap_cell(cell);
  }
  for (auto& entry : color_stack_) {
    if (entry.default_fg == old_fg)
      entry.default_fg = new_fg;
    if (entry.default_bg == old_bg)
      entry.default_bg = new_bg;
    if (entry.cursor_color == old_cursor)
      entry.cursor_color = theme_.cursor_color;
    for (size_t i = 0; i < entry.ansi_colors.size(); ++i) {
      if (entry.ansi_colors[i] == old_ansi[i])
        entry.ansi_colors[i] = new_ansi[i];
    }
  }
  MarkAllDirty();
}

void TerminalBuffer::Reset() {
  InitializeDefaultTheme();
  color_stack_.clear();
  ResetPen();
  cursor_row_ = 0;
  cursor_col_ = 0;
  wrap_pending_ = false;
  scroll_top_ = 0;
  scroll_bottom_ = rows_ - 1;
  scrollback_.clear();
  base_line_index_ = 0;
  scroll_offset_lines_ = 0;
  using_alt_screen_ = false;
  hyperlinks_.clear();
  graphic_placements_.clear();
  sync_snapshot_lines_.clear();
  sync_snapshot_graphics_.clear();

  cursor_visible_ = true;
  cursor_blink_ = true;
  cursor_style_ = 0;
  auto_wrap_mode_ = true;
  origin_mode_ = false;
  application_cursor_keys_ = false;
  bracketed_paste_mode_ = false;
  synchronized_output_ = false;
  in_band_resize_mode_ = false;
  focus_reporting_mode_ = false;
  mouse_tracking_mode_ = 0;
  mouse_encoding_mode_ = 0;
  dec_special_graphics_ = false;

  primary_screen_.assign(rows_, MakeBlankLine(cols_));
  alt_screen_.assign(rows_, MakeBlankLine(cols_));
  saved_primary_cursor_ = SavedCursorState{.pen = current_pen_};
  saved_alt_cursor_ = SavedCursorState{.pen = current_pen_};
  alt_screen_saved_cursor_ = SavedCursorState{.pen = current_pen_};
  MarkAllDirty();
}

Cell TerminalBuffer::MakeBlankCell() const {
  Cell cell;
  cell.codepoint = U' ';
  cell.fg_color = theme_.default_fg;
  cell.bg_color = current_pen_.bg_color;
  return cell;
}

Line TerminalBuffer::MakeBlankLine(int cols) const {
  Line line;
  line.cells.assign(cols, MakeBlankCell());
  line.wrapped = false;
  return line;
}

void TerminalBuffer::ResetPen() {
  uint16 active_link = current_pen_.hyperlink_id;
  current_pen_ = Cell{};
  current_pen_.fg_color = theme_.default_fg;
  current_pen_.bg_color = theme_.default_bg;
  current_pen_.hyperlink_id = active_link;
}

uint32 TerminalBuffer::GetAnsiColor(int index) const {
  if (index < 0 || index >= 16)
    return theme_.default_fg;
  return theme_.ansi_colors[index];
}

uint32 TerminalBuffer::GetColor256(int index) const {
  if (index < 0 || index > 255)
    return theme_.default_fg;
  if (index < kColorCubeStart)
    return theme_.ansi_colors[index];
  if (index < kGrayscaleRampStart) {
    int cube = index - kColorCubeStart;
    uint8 r = kCubeLevels[(cube / 36) % 6];
    uint8 g = kCubeLevels[(cube / 6) % 6];
    uint8 b = kCubeLevels[cube % 6];
    return 0xFF000000u | (static_cast<uint32>(r) << 16) |
           (static_cast<uint32>(g) << 8) | static_cast<uint32>(b);
  }
  uint8 v = static_cast<uint8>(kGrayscaleRampBase +
                               (index - kGrayscaleRampStart) *
                                   kGrayscaleRampStep);
  return 0xFF000000u | (static_cast<uint32>(v) << 16) |
         (static_cast<uint32>(v) << 8) | static_cast<uint32>(v);
}

void TerminalBuffer::SetDefaultForegroundColor(uint32 color) {
  if (active_theme_ == TerminalTheme::Light &&
      color == kDefaultTerminalForegroundColor)
    color = theme_default_fg_;
  uint32 old_fg = theme_.default_fg;
  theme_.default_fg = color;
  if (current_pen_.fg_color == old_fg)
    current_pen_.fg_color = color;
  for (auto& line : ActiveScreen()) {
    for (auto& cell : line.cells) {
      if (cell.fg_color == old_fg)
        cell.fg_color = color;
    }
  }
  MarkAllDirty();
}

void TerminalBuffer::SetDefaultBackgroundColor(uint32 color) {
  if (active_theme_ == TerminalTheme::Light &&
      color == kDefaultTerminalBackgroundColor)
    color = theme_default_bg_;
  uint32 old_bg = theme_.default_bg;
  theme_.default_bg = color;
  if (current_pen_.bg_color == old_bg)
    current_pen_.bg_color = color;
  for (auto& line : ActiveScreen()) {
    for (auto& cell : line.cells) {
      if (cell.bg_color == old_bg)
        cell.bg_color = color;
    }
  }
  MarkAllDirty();
}

void TerminalBuffer::SetCursorColor(uint32 color) {
  if (active_theme_ == TerminalTheme::Light &&
      color == kDefaultTerminalCursorColor)
    color = theme_default_cursor_;
  theme_.cursor_color = color;
  MarkRowDirty(cursor_row_);
}

void TerminalBuffer::ResetDefaultColors() {
  SetDefaultForegroundColor(theme_default_fg_);
  SetDefaultBackgroundColor(theme_default_bg_);
  SetCursorColor(theme_default_cursor_);
}

void TerminalBuffer::PushColorStack() {
  if (color_stack_.size() >= kMaxColorStackDepth)
    color_stack_.erase(color_stack_.begin());
  color_stack_.push_back(theme_);
}

void TerminalBuffer::PopColorStack() {
  if (color_stack_.empty())
    return;
  TerminalThemeState prev = color_stack_.back();
  color_stack_.pop_back();
  SetDefaultForegroundColor(prev.default_fg);
  SetDefaultBackgroundColor(prev.default_bg);
  SetCursorColor(prev.cursor_color);
  theme_.ansi_colors = prev.ansi_colors;
  MarkAllDirty();
}

void TerminalBuffer::Resize(int new_rows, int new_cols) {
  new_rows = std::max(1, new_rows);
  new_cols = std::max(1, new_cols);
  if (new_rows == rows_ && new_cols == cols_)
    return;

  if (new_cols != cols_) {
    ReflowPrimaryScreen(new_rows, new_cols);
  } else if (new_rows != rows_) {
    int primary_row =
        using_alt_screen_ ? alt_screen_saved_cursor_.row : cursor_row_;
    if (new_rows < rows_) {
      int excess = rows_ - new_rows;
      int push_to_scrollback = std::max(0, (primary_row + 1) - new_rows);
      push_to_scrollback = std::min(push_to_scrollback, excess);
      for (int i = 0; i < push_to_scrollback; ++i) {
        scrollback_.push_back(std::move(primary_screen_[i]));
        if (static_cast<int>(scrollback_.size()) > kMaxScrollbackLines) {
          scrollback_.pop_front();
          base_line_index_++;
        }
      }
      if (push_to_scrollback > 0) {
        primary_screen_.erase(primary_screen_.begin(),
                              primary_screen_.begin() + push_to_scrollback);
        if (using_alt_screen_) {
          alt_screen_saved_cursor_.row =
              std::max(0, alt_screen_saved_cursor_.row - push_to_scrollback);
          saved_primary_cursor_.row =
              std::max(0, saved_primary_cursor_.row - push_to_scrollback);
        } else {
          cursor_row_ = std::max(0, cursor_row_ - push_to_scrollback);
        }
      }
      primary_screen_.resize(new_rows, MakeBlankLine(new_cols));
    } else {
      int added = new_rows - rows_;
      int pull_from_scrollback =
          std::min(added, static_cast<int>(scrollback_.size()));
      for (int i = 0; i < pull_from_scrollback; ++i) {
        primary_screen_.insert(primary_screen_.begin(),
                               std::move(scrollback_.back()));
        scrollback_.pop_back();
        if (using_alt_screen_) {
          alt_screen_saved_cursor_.row =
              std::min(new_rows - 1, alt_screen_saved_cursor_.row + 1);
          saved_primary_cursor_.row =
              std::min(new_rows - 1, saved_primary_cursor_.row + 1);
        } else {
          cursor_row_++;
        }
      }
      while (static_cast<int>(primary_screen_.size()) < new_rows)
        primary_screen_.push_back(MakeBlankLine(new_cols));
    }
  }

  for (auto& line : alt_screen_) {
    if (static_cast<int>(line.cells.size()) < new_cols) {
      line.cells.resize(new_cols, MakeBlankCell());
    } else if (static_cast<int>(line.cells.size()) > new_cols) {
      line.cells.resize(new_cols);
    }
  }
  if (static_cast<int>(alt_screen_.size()) < new_rows) {
    alt_screen_.resize(new_rows, MakeBlankLine(new_cols));
  } else if (static_cast<int>(alt_screen_.size()) > new_rows) {
    alt_screen_.resize(new_rows);
  }

  if (synchronized_output_) {
    for (auto& line : sync_snapshot_lines_) {
      if (static_cast<int>(line.cells.size()) < new_cols)
        line.cells.resize(new_cols, MakeBlankCell());
      else if (static_cast<int>(line.cells.size()) > new_cols)
        line.cells.resize(new_cols);
    }
    if (static_cast<int>(sync_snapshot_lines_.size()) < new_rows)
      sync_snapshot_lines_.resize(new_rows, MakeBlankLine(new_cols));
    else if (static_cast<int>(sync_snapshot_lines_.size()) > new_rows)
      sync_snapshot_lines_.resize(new_rows);
  }

  rows_ = new_rows;
  cols_ = new_cols;
  cursor_row_ = std::clamp(cursor_row_, 0, rows_ - 1);
  cursor_col_ = std::clamp(cursor_col_, 0, cols_ - 1);
  wrap_pending_ = false;
  scroll_top_ = 0;
  scroll_bottom_ = rows_ - 1;
  scroll_offset_lines_ =
      std::clamp(scroll_offset_lines_, 0, ScrollbackSize());
  MarkAllDirty();
}

void TerminalBuffer::ReflowPrimaryScreen(int new_rows, int new_cols) {
  int saved_primary_row =
      using_alt_screen_ ? alt_screen_saved_cursor_.row : cursor_row_;
  int saved_primary_col =
      using_alt_screen_ ? alt_screen_saved_cursor_.col : cursor_col_;

  int last_non_blank_primary = saved_primary_row;
  for (int r = rows_ - 1; r > saved_primary_row; --r) {
    bool non_blank = primary_screen_[r].wrapped;
    if (!non_blank) {
      for (const auto& cell : primary_screen_[r].cells) {
        if (!IsDefaultBlankCell(cell, theme_.default_fg, theme_.default_bg)) {
          non_blank = true;
          break;
        }
      }
    }
    if (non_blank) {
      last_non_blank_primary = r;
      break;
    }
  }

  int total_source_lines =
      static_cast<int>(scrollback_.size()) + last_non_blank_primary + 1;
  int cursor_source_line =
      static_cast<int>(scrollback_.size()) + saved_primary_row;

  std::vector<Line> reflowed;
  reflowed.reserve(total_source_lines);

  int new_cursor_abs_line = 0;
  int new_cursor_col = 0;
  bool cursor_mapped = false;

  std::vector<Cell> logical_cells;
  int cursor_offset_in_logical = -1;

  auto flush_logical_line = [&]() {
    if (logical_cells.empty()) {
      Line blank;
      blank.cells.assign(new_cols, MakeBlankCell());
      blank.wrapped = false;
      if (cursor_offset_in_logical >= 0) {
        new_cursor_abs_line = static_cast<int>(reflowed.size());
        new_cursor_col = 0;
        cursor_mapped = true;
      }
      reflowed.push_back(std::move(blank));
      cursor_offset_in_logical = -1;
      return;
    }

    int idx = 0;
    int total = static_cast<int>(logical_cells.size());
    while (idx < total) {
      int chunk = std::min(new_cols, total - idx);
      Line phys;
      phys.cells.reserve(new_cols);
      for (int c = 0; c < chunk; ++c)
        phys.cells.push_back(logical_cells[idx + c]);
      if (static_cast<int>(phys.cells.size()) < new_cols)
        phys.cells.resize(new_cols, MakeBlankCell());
      phys.wrapped = (idx + chunk < total);

      if (cursor_offset_in_logical >= idx &&
          cursor_offset_in_logical < idx + chunk) {
        new_cursor_abs_line = static_cast<int>(reflowed.size());
        new_cursor_col = cursor_offset_in_logical - idx;
        cursor_mapped = true;
      } else if (cursor_offset_in_logical >= total && idx + chunk >= total) {
        new_cursor_abs_line = static_cast<int>(reflowed.size());
        new_cursor_col = std::min(new_cols - 1, cursor_offset_in_logical - idx);
        cursor_mapped = true;
      }

      reflowed.push_back(std::move(phys));
      idx += chunk;
    }
    logical_cells.clear();
    cursor_offset_in_logical = -1;
  };

  for (int src_idx = 0; src_idx < total_source_lines; ++src_idx) {
    const Line& src_line =
        (src_idx < static_cast<int>(scrollback_.size()))
            ? scrollback_[src_idx]
            : primary_screen_[src_idx - static_cast<int>(scrollback_.size())];

    int effective_len = static_cast<int>(src_line.cells.size());
    if (!src_line.wrapped) {
      while (effective_len > 0 &&
             IsDefaultBlankCell(src_line.cells[effective_len - 1],
                                theme_.default_fg, theme_.default_bg)) {
        if (src_idx == cursor_source_line &&
            effective_len - 1 <= saved_primary_col) {
          break;
        }
        effective_len--;
      }
    }

    if (src_idx == cursor_source_line) {
      cursor_offset_in_logical =
          static_cast<int>(logical_cells.size()) + saved_primary_col;
    }

    for (int c = 0; c < effective_len; ++c)
      logical_cells.push_back(src_line.cells[c]);

    if (!src_line.wrapped)
      flush_logical_line();
  }
  if (!logical_cells.empty() || cursor_offset_in_logical >= 0)
    flush_logical_line();

  scrollback_.clear();
  primary_screen_.clear();

  int total_reflowed = static_cast<int>(reflowed.size());
  int screen_start = std::max(0, total_reflowed - new_rows);
  if (cursor_mapped && new_cursor_abs_line < screen_start)
    screen_start = new_cursor_abs_line;

  for (int i = 0; i < screen_start; ++i) {
    scrollback_.push_back(std::move(reflowed[i]));
    if (static_cast<int>(scrollback_.size()) > kMaxScrollbackLines) {
      scrollback_.pop_front();
      base_line_index_++;
    }
  }

  for (int i = screen_start;
       i < total_reflowed && static_cast<int>(primary_screen_.size()) < new_rows;
       ++i) {
    primary_screen_.push_back(std::move(reflowed[i]));
  }
  while (static_cast<int>(primary_screen_.size()) < new_rows)
    primary_screen_.push_back(MakeBlankLine(new_cols));

  int final_cursor_row =
      cursor_mapped ? std::clamp(new_cursor_abs_line - screen_start, 0,
                                 new_rows - 1)
                    : 0;
  int final_cursor_col =
      cursor_mapped ? std::clamp(new_cursor_col, 0, new_cols - 1) : 0;

  if (using_alt_screen_) {
    alt_screen_saved_cursor_.row = final_cursor_row;
    alt_screen_saved_cursor_.col = final_cursor_col;
    saved_primary_cursor_.row = final_cursor_row;
    saved_primary_cursor_.col = final_cursor_col;
  } else {
    cursor_row_ = final_cursor_row;
    cursor_col_ = final_cursor_col;
  }
}

void TerminalBuffer::SetCursorPos(int row, int col) {
  MarkRowDirty(cursor_row_);
  int min_row = origin_mode_ ? scroll_top_ : 0;
  int max_row = origin_mode_ ? scroll_bottom_ : (rows_ - 1);
  int target_row = origin_mode_ ? (scroll_top_ + row) : row;
  cursor_row_ = std::clamp(target_row, min_row, max_row);
  cursor_col_ = std::clamp(col, 0, cols_ - 1);
  wrap_pending_ = false;
  MarkRowDirty(cursor_row_);
}

void TerminalBuffer::SetRawCursorPos(int row, int col) {
  MarkRowDirty(cursor_row_);
  cursor_row_ = std::clamp(row, 0, rows_ - 1);
  cursor_col_ = std::clamp(col, 0, cols_ - 1);
  wrap_pending_ = false;
  MarkRowDirty(cursor_row_);
}

void TerminalBuffer::MoveCursorRelative(int delta_row, int delta_col) {
  MarkRowDirty(cursor_row_);
  int min_row = (cursor_row_ >= scroll_top_ && cursor_row_ <= scroll_bottom_)
                    ? scroll_top_
                    : 0;
  int max_row = (cursor_row_ >= scroll_top_ && cursor_row_ <= scroll_bottom_)
                    ? scroll_bottom_
                    : (rows_ - 1);
  cursor_row_ = std::clamp(cursor_row_ + delta_row, min_row, max_row);
  cursor_col_ = std::clamp(cursor_col_ + delta_col, 0, cols_ - 1);
  wrap_pending_ = false;
  MarkRowDirty(cursor_row_);
}

void TerminalBuffer::SetScrollRegion(int top, int bottom) {
  top = std::clamp(top, 0, rows_ - 1);
  bottom = std::clamp(bottom, 0, rows_ - 1);
  if (top < bottom) {
    scroll_top_ = top;
    scroll_bottom_ = bottom;
  } else {
    scroll_top_ = 0;
    scroll_bottom_ = rows_ - 1;
  }
  SetCursorPos(0, 0);
}

void TerminalBuffer::SaveCursor() {
  SavedCursorState& target =
      using_alt_screen_ ? saved_alt_cursor_ : saved_primary_cursor_;
  target.row = cursor_row_;
  target.col = cursor_col_;
  target.pen = current_pen_;
  target.origin_mode = origin_mode_;
  target.auto_wrap_mode = auto_wrap_mode_;
  target.dec_special_graphics = dec_special_graphics_;
}

void TerminalBuffer::RestoreCursor() {
  MarkRowDirty(cursor_row_);
  const SavedCursorState& source =
      using_alt_screen_ ? saved_alt_cursor_ : saved_primary_cursor_;
  cursor_row_ = std::clamp(source.row, 0, rows_ - 1);
  cursor_col_ = std::clamp(source.col, 0, cols_ - 1);
  current_pen_ = source.pen;
  origin_mode_ = source.origin_mode;
  auto_wrap_mode_ = source.auto_wrap_mode;
  dec_special_graphics_ = source.dec_special_graphics;
  wrap_pending_ = false;
  MarkRowDirty(cursor_row_);
}

void TerminalBuffer::PutChar(char32_t codepoint) {
  if (dec_special_graphics_ && codepoint >= U'`' && codepoint <= U'~')
    codepoint = MapDecSpecialGraphics(codepoint);

  if (wrap_pending_) {
    if (auto_wrap_mode_) {
      ActiveScreen()[cursor_row_].wrapped = true;
      cursor_col_ = 0;
      Index();
    }
    wrap_pending_ = false;
  }

  Cell cell = current_pen_;
  cell.codepoint = codepoint;
  cell.scale = 1;
  cell.cell_width = 1;

  ActiveScreen()[cursor_row_].cells[cursor_col_] = cell;
  MarkRowDirty(cursor_row_);

  if (cursor_col_ + 1 < cols_) {
    cursor_col_++;
  } else if (auto_wrap_mode_) {
    wrap_pending_ = true;
  }
}

void TerminalBuffer::WriteScaledText(std::string_view utf8_text, int scale,
                                     int explicit_width) {
  scale = std::clamp(scale, 1, kMaxOsc66Scale);
  int char_cols = explicit_width > 0 ? std::clamp(explicit_width, 1, kMaxOsc66Scale)
                                     : scale;
  int char_rows = scale;

  size_t index = 0;
  while (index < utf8_text.size()) {
    char32_t cp = NextUtf8Codepoint(utf8_text, index);
    if (cp == 0)
      break;

    if (cursor_col_ + char_cols > cols_) {
      cursor_col_ = 0;
      for (int r = 0; r < char_rows; ++r)
        Index();
    }
    while (cursor_row_ + char_rows > rows_) {
      ScrollRegionUpOneLine(scroll_top_, scroll_bottom_);
      if (cursor_row_ > 0)
        cursor_row_--;
    }

    for (int dr = 0; dr < char_rows && cursor_row_ + dr < rows_; ++dr) {
      MarkRowDirty(cursor_row_ + dr);
      for (int dc = 0; dc < char_cols && cursor_col_ + dc < cols_; ++dc) {
        Cell& target = ActiveScreen()[cursor_row_ + dr].cells[cursor_col_ + dc];
        target = current_pen_;
        if (dr == 0 && dc == 0) {
          target.codepoint = cp;
          target.scale = static_cast<uint8>(scale);
          target.cell_width =
              static_cast<uint8>(std::max(1, char_cols / scale));
        } else {
          target.codepoint = U' ';
          target.scale = 0;
          target.cell_width = 1;
        }
      }
    }
    cursor_col_ = std::min(cols_ - 1, cursor_col_ + char_cols);
  }
}

void TerminalBuffer::NewLine(bool carriage_return) {
  MarkRowDirty(cursor_row_);
  if (carriage_return)
    cursor_col_ = 0;
  wrap_pending_ = false;
  Index();
}

void TerminalBuffer::CarriageReturn() {
  MarkRowDirty(cursor_row_);
  cursor_col_ = 0;
  wrap_pending_ = false;
}

void TerminalBuffer::Backspace() {
  MarkRowDirty(cursor_row_);
  if (wrap_pending_) {
    wrap_pending_ = false;
    return;
  }
  if (cursor_col_ > 0)
    cursor_col_--;
}

void TerminalBuffer::Tab() {
  MarkRowDirty(cursor_row_);
  wrap_pending_ = false;
  int next_tab = ((cursor_col_ / kTabStopWidth) + 1) * kTabStopWidth;
  cursor_col_ = std::min(cols_ - 1, next_tab);
}

void TerminalBuffer::ReverseIndex() {
  MarkRowDirty(cursor_row_);
  wrap_pending_ = false;
  if (cursor_row_ == scroll_top_) {
    ScrollRegionDownOneLine(scroll_top_, scroll_bottom_);
  } else if (cursor_row_ > 0) {
    cursor_row_--;
  }
  MarkRowDirty(cursor_row_);
}

void TerminalBuffer::Index() {
  MarkRowDirty(cursor_row_);
  wrap_pending_ = false;
  if (cursor_row_ == scroll_bottom_) {
    ScrollRegionUpOneLine(scroll_top_, scroll_bottom_);
  } else if (cursor_row_ + 1 < rows_) {
    cursor_row_++;
  }
  MarkRowDirty(cursor_row_);
}

void TerminalBuffer::ScrollRegionUpOneLine(int top, int bottom) {
  if (top < 0 || bottom >= rows_ || top > bottom)
    return;

  auto& screen = ActiveScreen();
  if (!using_alt_screen_ && top == 0 && bottom == rows_ - 1) {
    scrollback_.push_back(std::move(screen[0]));
    if (static_cast<int>(scrollback_.size()) > kMaxScrollbackLines) {
      scrollback_.pop_front();
      base_line_index_++;
      int64 min_valid_id = base_line_index_ - rows_;
      graphic_placements_.erase(
          std::remove_if(graphic_placements_.begin(), graphic_placements_.end(),
                         [min_valid_id](const TerminalGraphicPlacement& p) {
                           return !p.is_alt_screen &&
                                  p.anchor_line_id < min_valid_id;
                         }),
          graphic_placements_.end());
    }
    if (scroll_offset_lines_ > 0) {
      scroll_offset_lines_ =
          std::min(static_cast<int>(scrollback_.size()),
                   scroll_offset_lines_ + 1);
    }
  }

  for (int r = top; r < bottom; ++r)
    screen[r] = std::move(screen[r + 1]);
  screen[bottom] = MakeBlankLine(cols_);
  MarkAllDirty();
}

void TerminalBuffer::ScrollRegionDownOneLine(int top, int bottom) {
  if (top < 0 || bottom >= rows_ || top > bottom)
    return;

  auto& screen = ActiveScreen();
  for (int r = bottom; r > top; --r)
    screen[r] = std::move(screen[r - 1]);
  screen[top] = MakeBlankLine(cols_);
  MarkAllDirty();
}

void TerminalBuffer::ScrollUp(int count) {
  count = std::clamp(count, 1, rows_);
  for (int i = 0; i < count; ++i)
    ScrollRegionUpOneLine(scroll_top_, scroll_bottom_);
}

void TerminalBuffer::ScrollDown(int count) {
  count = std::clamp(count, 1, rows_);
  for (int i = 0; i < count; ++i)
    ScrollRegionDownOneLine(scroll_top_, scroll_bottom_);
}

void TerminalBuffer::EraseInDisplay(int mode) {
  auto& screen = ActiveScreen();
  Cell blank = MakeBlankCell();

  if (mode == 0) {
    for (int c = cursor_col_; c < cols_; ++c)
      screen[cursor_row_].cells[c] = blank;
    screen[cursor_row_].wrapped = false;
    MarkRowDirty(cursor_row_);
    for (int r = cursor_row_ + 1; r < rows_; ++r) {
      screen[r] = MakeBlankLine(cols_);
      MarkRowDirty(r);
    }
  } else if (mode == 1) {
    for (int r = 0; r < cursor_row_; ++r) {
      screen[r] = MakeBlankLine(cols_);
      MarkRowDirty(r);
    }
    for (int c = 0; c <= cursor_col_ && c < cols_; ++c)
      screen[cursor_row_].cells[c] = blank;
    MarkRowDirty(cursor_row_);
  } else if (mode == 2 || mode == 3) {
    for (int r = 0; r < rows_; ++r)
      screen[r] = MakeBlankLine(cols_);
    if (mode == 3 && !using_alt_screen_) {
      scrollback_.clear();
      scroll_offset_lines_ = 0;
    }
    graphic_placements_.erase(
        std::remove_if(graphic_placements_.begin(), graphic_placements_.end(),
                       [this](const TerminalGraphicPlacement& p) {
                         return p.is_alt_screen == using_alt_screen_;
                       }),
        graphic_placements_.end());
    MarkAllDirty();
  }
}

void TerminalBuffer::EraseInLine(int mode) {
  auto& screen = ActiveScreen();
  Cell blank = MakeBlankCell();
  Line& line = screen[cursor_row_];

  if (mode == 0) {
    for (int c = cursor_col_; c < cols_; ++c)
      line.cells[c] = blank;
    line.wrapped = false;
  } else if (mode == 1) {
    for (int c = 0; c <= cursor_col_ && c < cols_; ++c)
      line.cells[c] = blank;
  } else if (mode == 2) {
    for (int c = 0; c < cols_; ++c)
      line.cells[c] = blank;
    line.wrapped = false;
  }
  MarkRowDirty(cursor_row_);
}

void TerminalBuffer::InsertLines(int count) {
  if (cursor_row_ < scroll_top_ || cursor_row_ > scroll_bottom_)
    return;
  count = std::clamp(count, 1, scroll_bottom_ - cursor_row_ + 1);
  for (int i = 0; i < count; ++i)
    ScrollRegionDownOneLine(cursor_row_, scroll_bottom_);
}

void TerminalBuffer::DeleteLines(int count) {
  if (cursor_row_ < scroll_top_ || cursor_row_ > scroll_bottom_)
    return;
  count = std::clamp(count, 1, scroll_bottom_ - cursor_row_ + 1);
  for (int i = 0; i < count; ++i)
    ScrollRegionUpOneLine(cursor_row_, scroll_bottom_);
}

void TerminalBuffer::InsertChars(int count) {
  count = std::clamp(count, 1, cols_ - cursor_col_);
  auto& cells = ActiveScreen()[cursor_row_].cells;
  for (int c = cols_ - 1; c >= cursor_col_ + count; --c)
    cells[c] = cells[c - count];
  Cell blank = MakeBlankCell();
  for (int c = cursor_col_; c < cursor_col_ + count; ++c)
    cells[c] = blank;
  MarkRowDirty(cursor_row_);
}

void TerminalBuffer::DeleteChars(int count) {
  count = std::clamp(count, 1, cols_ - cursor_col_);
  auto& cells = ActiveScreen()[cursor_row_].cells;
  for (int c = cursor_col_; c < cols_ - count; ++c)
    cells[c] = cells[c + count];
  Cell blank = MakeBlankCell();
  for (int c = cols_ - count; c < cols_; ++c)
    cells[c] = blank;
  MarkRowDirty(cursor_row_);
}

void TerminalBuffer::EraseChars(int count) {
  count = std::clamp(count, 1, cols_ - cursor_col_);
  auto& cells = ActiveScreen()[cursor_row_].cells;
  Cell blank = MakeBlankCell();
  for (int c = cursor_col_; c < cursor_col_ + count; ++c)
    cells[c] = blank;
  MarkRowDirty(cursor_row_);
}

void TerminalBuffer::SetAlternateScreen(bool enable, bool save_restore_cursor,
                                        bool clear_alt) {
  if (enable == using_alt_screen_)
    return;

  if (enable) {
    alt_screen_saved_cursor_.row = cursor_row_;
    alt_screen_saved_cursor_.col = cursor_col_;
    alt_screen_saved_cursor_.pen = current_pen_;
    alt_screen_saved_cursor_.origin_mode = origin_mode_;
    alt_screen_saved_cursor_.auto_wrap_mode = auto_wrap_mode_;
    alt_screen_saved_cursor_.dec_special_graphics = dec_special_graphics_;
    if (save_restore_cursor)
      saved_primary_cursor_ = alt_screen_saved_cursor_;

    using_alt_screen_ = true;
    scroll_top_ = 0;
    scroll_bottom_ = rows_ - 1;
    origin_mode_ = false;
    scroll_offset_lines_ = 0;
    cursor_row_ = 0;
    cursor_col_ = 0;
    wrap_pending_ = false;
    dec_special_graphics_ = false;
    ResetPen();
    saved_alt_cursor_ = SavedCursorState{.pen = current_pen_};

    alt_screen_.assign(rows_, MakeBlankLine(cols_));
    graphic_placements_.erase(
        std::remove_if(graphic_placements_.begin(), graphic_placements_.end(),
                       [](const TerminalGraphicPlacement& p) {
                         return p.is_alt_screen;
                       }),
        graphic_placements_.end());
  } else {
    if (clear_alt) {
      alt_screen_.assign(rows_, MakeBlankLine(cols_));
      graphic_placements_.erase(
          std::remove_if(graphic_placements_.begin(), graphic_placements_.end(),
                         [](const TerminalGraphicPlacement& p) {
                           return p.is_alt_screen;
                         }),
          graphic_placements_.end());
    }
    using_alt_screen_ = false;
    scroll_top_ = 0;
    scroll_bottom_ = rows_ - 1;
    origin_mode_ = false;
    scroll_offset_lines_ = 0;
    wrap_pending_ = false;
    if (save_restore_cursor) {
      cursor_row_ = std::clamp(alt_screen_saved_cursor_.row, 0, rows_ - 1);
      cursor_col_ = std::clamp(alt_screen_saved_cursor_.col, 0, cols_ - 1);
      current_pen_ = alt_screen_saved_cursor_.pen;
      origin_mode_ = alt_screen_saved_cursor_.origin_mode;
      auto_wrap_mode_ = alt_screen_saved_cursor_.auto_wrap_mode;
      dec_special_graphics_ = alt_screen_saved_cursor_.dec_special_graphics;
    } else {
      cursor_row_ = std::clamp(cursor_row_, 0, rows_ - 1);
      cursor_col_ = std::clamp(cursor_col_, 0, cols_ - 1);
    }
  }
  MarkAllDirty();
}

void TerminalBuffer::SetScrollOffsetLines(int offset) {
  if (using_alt_screen_) {
    scroll_offset_lines_ = 0;
    return;
  }
  int clamped = std::clamp(offset, 0, ScrollbackSize());
  if (scroll_offset_lines_ != clamped) {
    scroll_offset_lines_ = clamped;
    MarkAllDirty();
  }
}

void TerminalBuffer::ClearScrollback() {
  scrollback_.clear();
  scroll_offset_lines_ = 0;
  MarkAllDirty();
}

const Line& TerminalBuffer::GetVisibleLine(int visible_row) const {
  if (synchronized_output_ && !sync_snapshot_lines_.empty()) {
    int idx = std::clamp(visible_row, 0,
                         static_cast<int>(sync_snapshot_lines_.size()) - 1);
    return sync_snapshot_lines_[idx];
  }
  visible_row = std::clamp(visible_row, 0, rows_ - 1);
  if (using_alt_screen_)
    return alt_screen_[visible_row];
  int abs_line = ScrollbackSize() - scroll_offset_lines_ + visible_row;
  return GetAbsoluteLine(abs_line);
}

const Line& TerminalBuffer::GetAbsoluteLine(int absolute_line) const {
  if (using_alt_screen_)
    return alt_screen_[std::clamp(absolute_line, 0, rows_ - 1)];

  int sb_size = static_cast<int>(scrollback_.size());
  if (absolute_line < 0)
    return scrollback_.empty() ? primary_screen_[0] : scrollback_[0];
  if (absolute_line < sb_size)
    return scrollback_[absolute_line];
  int screen_row = std::clamp(absolute_line - sb_size, 0, rows_ - 1);
  return primary_screen_[screen_row];
}

int64 TerminalBuffer::TopVisibleLineId() const {
  if (using_alt_screen_)
    return 0;
  return base_line_index_ +
         static_cast<int64>(ScrollbackSize() - scroll_offset_lines_);
}

int64 TerminalBuffer::CurrentCursorLineId() const {
  if (using_alt_screen_)
    return cursor_row_;
  return base_line_index_ + static_cast<int64>(ScrollbackSize() + cursor_row_);
}

uint16 TerminalBuffer::SetHyperlink(std::string_view url) {
  if (url.empty()) {
    current_pen_.hyperlink_id = 0;
    return 0;
  }
  for (size_t i = 0; i < hyperlinks_.size(); ++i) {
    if (hyperlinks_[i] == url) {
      uint16 id = static_cast<uint16>(i + 1);
      current_pen_.hyperlink_id = id;
      return id;
    }
  }
  if (hyperlinks_.size() >= kMaxHyperlinks) {
    current_pen_.hyperlink_id = 0;
    return 0;
  }
  hyperlinks_.emplace_back(url);
  uint16 id = static_cast<uint16>(hyperlinks_.size());
  current_pen_.hyperlink_id = id;
  return id;
}

std::string_view TerminalBuffer::GetHyperlinkUrl(uint16 id) const {
  if (id == 0 || id > hyperlinks_.size())
    return "";
  return hyperlinks_[id - 1];
}

void TerminalBuffer::AddGraphicPlacement(
    const TerminalGraphicPlacement& placement) {
  if (placement.image_id != 0 && placement.placement_id != 0) {
    graphic_placements_.erase(
        std::remove_if(graphic_placements_.begin(), graphic_placements_.end(),
                       [&](const TerminalGraphicPlacement& p) {
                         return p.image_id == placement.image_id &&
                                p.placement_id == placement.placement_id &&
                                p.is_alt_screen == placement.is_alt_screen;
                       }),
        graphic_placements_.end());
  }
  graphic_placements_.push_back(placement);
  MarkAllDirty();
}

void TerminalBuffer::DeleteGraphicPlacements(char selector, uint32 image_id,
                                             uint32 placement_id,
                                             int32 z_index) {
  char lower_sel = (selector >= 'A' && selector <= 'Z')
                       ? static_cast<char>(selector - 'A' + 'a')
                       : selector;
  graphic_placements_.erase(
      std::remove_if(graphic_placements_.begin(), graphic_placements_.end(),
                     [&](const TerminalGraphicPlacement& p) {
                       if (p.is_alt_screen != using_alt_screen_)
                         return false;
                       if (lower_sel == 'a')
                         return true;
                       if (lower_sel == 'i') {
                         if (p.image_id != image_id)
                           return false;
                         return placement_id == 0 ||
                                p.placement_id == placement_id;
                       }
                       if (lower_sel == 'z')
                         return p.z_index == z_index;
                       return false;
                     }),
      graphic_placements_.end());
  MarkAllDirty();
}

void TerminalBuffer::SetCursorVisible(bool visible) {
  if (cursor_visible_ != visible) {
    cursor_visible_ = visible;
    MarkRowDirty(cursor_row_);
  }
}

void TerminalBuffer::SetCursorBlinking(bool blink) {
  if (cursor_blink_ != blink) {
    cursor_blink_ = blink;
    MarkRowDirty(cursor_row_);
  }
}

void TerminalBuffer::SetCursorStyle(int style) {
  style = std::clamp(style, 0, 6);
  bool blink = (style == 0 || style == 1 || style == 3 || style == 5);
  if (cursor_style_ != style || cursor_blink_ != blink) {
    cursor_style_ = style;
    cursor_blink_ = blink;
    MarkRowDirty(cursor_row_);
  }
}

void TerminalBuffer::SetOriginMode(bool enable) {
  origin_mode_ = enable;
  SetCursorPos(0, 0);
}

void TerminalBuffer::SetSynchronizedOutput(bool enable) {
  if (enable == synchronized_output_)
    return;
  if (enable) {
    sync_snapshot_lines_.clear();
    sync_snapshot_lines_.reserve(rows_);
    for (int r = 0; r < rows_; ++r)
      sync_snapshot_lines_.push_back(GetVisibleLine(r));
    sync_snapshot_graphics_ = graphic_placements_;
    synchronized_output_ = true;
  } else {
    synchronized_output_ = false;
    sync_snapshot_lines_.clear();
    sync_snapshot_graphics_.clear();
    MarkAllDirty();
  }
}

void TerminalBuffer::MarkRowDirty(int row) {
  row = std::clamp(row, 0, rows_ - 1);
  if (min_dirty_row_ > max_dirty_row_) {
    min_dirty_row_ = row;
    max_dirty_row_ = row;
  } else {
    min_dirty_row_ = std::min(min_dirty_row_, row);
    max_dirty_row_ = std::max(max_dirty_row_, row);
  }
}

void TerminalBuffer::MarkAllDirty() {
  min_dirty_row_ = 0;
  max_dirty_row_ = rows_ - 1;
  full_dirty_ = true;
}

bool TerminalBuffer::ConsumeDirtyRows(int& min_row, int& max_row,
                                      bool& full_dirty) {
  if (!full_dirty_ && min_dirty_row_ > max_dirty_row_)
    return false;
  min_row = std::clamp(min_dirty_row_, 0, rows_ - 1);
  max_row = std::clamp(max_dirty_row_, 0, rows_ - 1);
  full_dirty = full_dirty_;
  min_dirty_row_ = rows_;
  max_dirty_row_ = -1;
  full_dirty_ = false;
  return true;
}

std::string TerminalBuffer::ExtractText(int start_abs_line, int start_col,
                                        int end_abs_line, int end_col) const {
  if (start_abs_line > end_abs_line ||
      (start_abs_line == end_abs_line && start_col > end_col)) {
    std::swap(start_abs_line, end_abs_line);
    std::swap(start_col, end_col);
  }

  int total = TotalLines();
  start_abs_line = std::clamp(start_abs_line, 0, total - 1);
  end_abs_line = std::clamp(end_abs_line, 0, total - 1);

  std::string result;
  for (int line_idx = start_abs_line; line_idx <= end_abs_line; ++line_idx) {
    const Line& line = GetAbsoluteLine(line_idx);
    int c_start = (line_idx == start_abs_line)
                      ? std::clamp(start_col, 0, cols_ - 1)
                      : 0;
    int c_end = (line_idx == end_abs_line)
                    ? std::clamp(end_col, 0, cols_ - 1)
                    : (cols_ - 1);

    std::string line_str;
    for (int c = c_start; c <= c_end && c < static_cast<int>(line.cells.size());
         ++c) {
      if (line.cells[c].scale == 0)
        continue;
      char32_t cp = line.cells[c].codepoint;
      if (cp == 0)
        cp = U' ';
      AppendCodepointUtf8(cp, line_str);
    }

    if (line_idx < end_abs_line || c_end == cols_ - 1) {
      while (!line_str.empty() && line_str.back() == ' ')
        line_str.pop_back();
    }

    result.append(line_str);
    if (line_idx < end_abs_line && !line.wrapped)
      result.push_back('\n');
  }
  return result;
}
