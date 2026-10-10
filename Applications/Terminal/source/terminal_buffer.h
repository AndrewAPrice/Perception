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

#include <array>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "include/core/SkImage.h"
#include "include/core/SkRefCnt.h"
#include "types.h"

namespace {

// Default deep slate-navy background color for the dark terminal theme.
constexpr uint32 kDefaultTerminalBackgroundColor = 0xFF1E1E2E;

// Default crisp soft-white foreground color for the dark terminal theme.
constexpr uint32 kDefaultTerminalForegroundColor = 0xFFCDD6F4;

// Default warm rosewater color for the dark terminal cursor.
constexpr uint32 kDefaultTerminalCursorColor = 0xFFF5E0DC;

// Semi-transparent selection highlight color for the dark terminal theme.
constexpr uint32 kTerminalSelectionHighlightColor = 0x6689B4FA;

// Default soft off-white background color for the light terminal theme.
constexpr uint32 kLightTerminalBackgroundColor = 0xFFEFF1F5;

// Default dark slate foreground color for the light terminal theme.
constexpr uint32 kLightTerminalForegroundColor = 0xFF4C4F69;

// Default warm rosewater color for the light terminal cursor.
constexpr uint32 kLightTerminalCursorColor = 0xFFDC8A78;

// Semi-transparent selection highlight color for the light terminal theme.
constexpr uint32 kLightTerminalSelectionHighlightColor = 0x667287FD;

// Default number of columns in the terminal grid.
constexpr int kDefaultTerminalCols = 80;

// Default number of rows in the terminal grid.
constexpr int kDefaultTerminalRows = 25;

// Maximum number of lines retained in the primary scrollback ring buffer.
constexpr int kMaxScrollbackLines = 2000;

// Maximum depth of the Kitty color theme stack (OSC 30001 / 30101).
constexpr size_t kMaxColorStackDepth = 16;

// Maximum number of active interned OSC 8 hyperlinks.
constexpr size_t kMaxHyperlinks = 65535;

// Bold text style flag.
constexpr uint16 kCellFlagBold = 1 << 0;

// Dimmed text alpha flag.
constexpr uint16 kCellFlagDim = 1 << 1;

// Italic text style flag.
constexpr uint16 kCellFlagItalic = 1 << 2;

// Inverse video (swapped foreground and background) flag.
constexpr uint16 kCellFlagInverse = 1 << 3;

// Hidden (invisible glyph) flag.
constexpr uint16 kCellFlagHidden = 1 << 4;

// Horizontal strikethrough line flag.
constexpr uint16 kCellFlagStrikethrough = 1 << 5;

// Blinking text attribute flag.
constexpr uint16 kCellFlagBlink = 1 << 6;

}  // namespace

// Built-in color themes supported by the terminal.
enum class TerminalTheme {
  Dark = 0,
  Light = 1,
};

// Represents a single character cell in the terminal grid.
struct Cell {
  char32_t codepoint = U' ';
  uint32 fg_color = kDefaultTerminalForegroundColor;
  uint32 bg_color = kDefaultTerminalBackgroundColor;
  uint32 underline_color = 0;
  uint8 font_family = 0;
  uint8 underline_style = 0;
  uint8 scale = 1;
  uint8 cell_width = 1;
  uint16 flags = 0;
  uint16 hyperlink_id = 0;
};

// Represents a single horizontal row of cells in the screen or scrollback.
struct Line {
  std::vector<Cell> cells;
  bool wrapped = false;
};

// Represents an inline Sixel or Kitty Graphics image placement anchored to the grid.
struct TerminalGraphicPlacement {
  uint32 image_id = 0;
  uint32 placement_id = 0;
  sk_sp<SkImage> image;
  int64 anchor_line_id = 0;
  int anchor_col = 0;
  int display_cols = 0;
  int display_rows = 0;
  int crop_x = 0;
  int crop_y = 0;
  int crop_w = 0;
  int crop_h = 0;
  int x_offset = 0;
  int y_offset = 0;
  int32 z_index = -1;
  bool is_alt_screen = false;
};

// Stores the active default colors and 16-color ANSI palette.
struct TerminalThemeState {
  uint32 default_fg = kDefaultTerminalForegroundColor;
  uint32 default_bg = kDefaultTerminalBackgroundColor;
  uint32 cursor_color = kDefaultTerminalCursorColor;
  std::array<uint32, 16> ansi_colors;
};

// Stores saved cursor and pen state for DECSC / DECRC and ?1049h / ?1049l.
struct SavedCursorState {
  int row = 0;
  int col = 0;
  Cell pen;
  bool origin_mode = false;
  bool auto_wrap_mode = true;
  bool dec_special_graphics = false;
};

// Manages the primary and alternate screen buffers, scrollback history,
// reflow on resize, SGR pen state, color stack, hyperlinks, and graphics placements.
class TerminalBuffer {
 public:
  // Constructs a terminal buffer with the specified initial grid dimensions.
  TerminalBuffer(int rows = kDefaultTerminalRows,
                 int cols = kDefaultTerminalCols);

  // Resets all terminal state, screens, scrollback, and modes to defaults.
  void Reset();

  // Resizes the terminal grid, reflowing soft-wrapped lines on the primary screen.
  void Resize(int new_rows, int new_cols);

  // Returns the number of visible rows in the terminal grid.
  int Rows() const { return rows_; }

  // Returns the number of columns in the terminal grid.
  int Cols() const { return cols_; }

  // Returns the current 0-based cursor row on the active screen.
  int CursorRow() const { return cursor_row_; }

  // Returns the current 0-based cursor column on the active screen.
  int CursorCol() const { return cursor_col_; }

  // Moves the cursor to (row, col), respecting origin mode (DECOM) if active.
  void SetCursorPos(int row, int col);

  // Moves the cursor to absolute 0-based screen coordinates (row, col).
  void SetRawCursorPos(int row, int col);

  // Moves the cursor relative to its current position, clamped to margins/screen.
  void MoveCursorRelative(int delta_row, int delta_col);

  // Sets the top and bottom scrolling margins (0-based inclusive).
  void SetScrollRegion(int top, int bottom);

  // Returns the top row of the active scrolling region.
  int ScrollTop() const { return scroll_top_; }

  // Returns the bottom row of the active scrolling region.
  int ScrollBottom() const { return scroll_bottom_; }

  // Saves the current cursor position and SGR pen state.
  void SaveCursor();

  // Restores the previously saved cursor position and SGR pen state.
  void RestoreCursor();

  // Writes a single Unicode codepoint at the cursor and advances the cursor.
  void PutChar(char32_t codepoint);

  // Writes multi-cell scaled text using the Kitty Text Sizing Protocol (OSC 66).
  void WriteScaledText(std::string_view utf8_text, int scale,
                       int explicit_width);

  // Advances the cursor down one line, scrolling if at the bottom margin.
  void NewLine(bool carriage_return);

  // Moves the cursor to column 0 on the current row.
  void CarriageReturn();

  // Moves the cursor one column to the left if not at column 0.
  void Backspace();

  // Advances the cursor to the next 8-column tab stop.
  void Tab();

  // Moves the cursor up one line, scrolling down if at the top margin (RI).
  void ReverseIndex();

  // Moves the cursor down one line, scrolling up if at the bottom margin (IND).
  void Index();

  // Scrolls the active scrolling region up by `count` lines.
  void ScrollUp(int count);

  // Scrolls the active scrolling region down by `count` lines.
  void ScrollDown(int count);

  // Erases part or all of the display (0=below, 1=above, 2=all, 3=all+scrollback).
  void EraseInDisplay(int mode);

  // Erases part or all of the current line (0=right, 1=left, 2=all).
  void EraseInLine(int mode);

  // Inserts `count` blank lines at the cursor row within the scroll region.
  void InsertLines(int count);

  // Deletes `count` lines at the cursor row within the scroll region.
  void DeleteLines(int count);

  // Inserts `count` blank characters at the cursor column on the current row.
  void InsertChars(int count);

  // Deletes `count` characters at the cursor column on the current row.
  void DeleteChars(int count);

  // Erases `count` characters starting at the cursor column without shifting.
  void EraseChars(int count);

  // Returns a mutable reference to the active SGR pen template cell.
  Cell& CurrentPen() { return current_pen_; }

  // Returns a const reference to the active SGR pen template cell.
  const Cell& CurrentPen() const { return current_pen_; }

  // Resets SGR attributes on the pen while preserving active default colors.
  void ResetPen();

  // Returns the ARGB color for a 16-color ANSI palette index (0..15).
  uint32 GetAnsiColor(int index) const;

  // Returns the ARGB color for a 256-color palette index (0..255).
  uint32 GetColor256(int index) const;

  // Returns the active default foreground color.
  uint32 GetDefaultForegroundColor() const { return theme_.default_fg; }

  // Returns the active default background color.
  uint32 GetDefaultBackgroundColor() const { return theme_.default_bg; }

  // Returns the active cursor color.
  uint32 GetCursorColor() const { return theme_.cursor_color; }

  // Returns the active selection highlight color.
  uint32 GetSelectionHighlightColor() const {
    return selection_highlight_color_;
  }

  // Returns the active terminal color theme (Dark or Light).
  TerminalTheme GetTheme() const { return active_theme_; }

  // Applies a built-in terminal color theme (Dark or Light) and updates cells.
  void ApplyTheme(TerminalTheme theme);

  // Dynamically overrides the default foreground color (OSC 10).
  void SetDefaultForegroundColor(uint32 color);

  // Dynamically overrides the default background color (OSC 11).
  void SetDefaultBackgroundColor(uint32 color);

  // Dynamically overrides the cursor color (OSC 12).
  void SetCursorColor(uint32 color);

  // Resets default foreground, background, and cursor colors to initial values.
  void ResetDefaultColors();

  // Pushes the active color theme onto the Kitty color stack (OSC 30001).
  void PushColorStack();

  // Pops and restores the top color theme from the Kitty color stack (OSC 30101).
  void PopColorStack();

  // Switches between the primary and alternate screen buffers (?47 / ?1047 / ?1049).
  void SetAlternateScreen(bool enable, bool save_restore_cursor,
                          bool clear_alt);

  // Returns whether the alternate screen buffer is currently active.
  bool IsAlternateScreen() const { return using_alt_screen_; }

  // Returns the number of lines currently stored in the primary scrollback buffer.
  int ScrollbackSize() const {
    return using_alt_screen_ ? 0 : static_cast<int>(scrollback_.size());
  }

  // Returns the total number of lines (scrollback + active screen rows).
  int TotalLines() const { return ScrollbackSize() + rows_; }

  // Returns how many lines above the live bottom the viewport is scrolled.
  int ScrollOffsetLines() const {
    return using_alt_screen_ ? 0 : scroll_offset_lines_;
  }

  // Sets how many lines above the live bottom the viewport is scrolled.
  void SetScrollOffsetLines(int offset);

  // Clears the scrollback history buffer and resets the scroll offset.
  void ClearScrollback();

  // Returns the line at `visible_row` (0 .. Rows() - 1) taking scroll offset into account.
  const Line& GetVisibleLine(int visible_row) const;

  // Returns the line at `absolute_line` (0 .. TotalLines() - 1).
  const Line& GetAbsoluteLine(int absolute_line) const;

  // Returns the monotonic line ID corresponding to visible row 0.
  int64 TopVisibleLineId() const;

  // Returns the monotonic line ID corresponding to the current cursor row.
  int64 CurrentCursorLineId() const;

  // Interns or clears the active OSC 8 hyperlink URL and sets it on the pen.
  uint16 SetHyperlink(std::string_view url);

  // Looks up the URL string for a non-zero hyperlink ID.
  std::string_view GetHyperlinkUrl(uint16 id) const;

  // Adds an inline Sixel or Kitty graphic placement to the buffer.
  void AddGraphicPlacement(const TerminalGraphicPlacement& placement);

  // Deletes matching Kitty graphic placements according to selector `d=`.
  void DeleteGraphicPlacements(char selector, uint32 image_id,
                               uint32 placement_id, int32 z_index);

  // Returns all active graphic placements.
  const std::vector<TerminalGraphicPlacement>& GetGraphicPlacements() const {
    return synchronized_output_ ? sync_snapshot_graphics_
                                : graphic_placements_;
  }

  // Returns whether the cursor is visible (?25h).
  bool IsCursorVisible() const { return cursor_visible_; }

  // Sets whether the cursor is visible (?25h / ?25l).
  void SetCursorVisible(bool visible);

  // Returns whether the cursor blinks (?12h or odd/0 DECSCUSR styles).
  bool IsCursorBlinking() const { return cursor_blink_; }

  // Sets whether the cursor blinks (?12h / ?12l).
  void SetCursorBlinking(bool blink);

  // Returns the DECSCUSR cursor style (0/1/2=block, 3/4=underline, 5/6=bar).
  int CursorStyle() const { return cursor_style_; }

  // Sets the DECSCUSR cursor style.
  void SetCursorStyle(int style);

  // Returns whether auto-wrap mode (?7h) is enabled.
  bool IsAutoWrap() const { return auto_wrap_mode_; }

  // Sets whether auto-wrap mode (?7h / ?7l) is enabled.
  void SetAutoWrap(bool enable) { auto_wrap_mode_ = enable; }

  // Returns whether origin mode (?6h) is enabled.
  bool IsOriginMode() const { return origin_mode_; }

  // Sets whether origin mode (?6h / ?6l) is enabled and homes the cursor.
  void SetOriginMode(bool enable);

  // Returns whether application cursor keys (?1h) are enabled.
  bool IsApplicationCursorKeys() const { return application_cursor_keys_; }

  // Sets whether application cursor keys (?1h / ?1l) are enabled.
  void SetApplicationCursorKeys(bool enable) {
    application_cursor_keys_ = enable;
  }

  // Returns whether bracketed paste mode (?2004h) is enabled.
  bool IsBracketedPaste() const { return bracketed_paste_mode_; }

  // Sets whether bracketed paste mode (?2004h / ?2004l) is enabled.
  void SetBracketedPaste(bool enable) { bracketed_paste_mode_ = enable; }

  // Returns whether synchronized output (?2026h) is currently active.
  bool IsSynchronizedOutput() const { return synchronized_output_; }

  // Sets whether synchronized output (?2026h / ?2026l) is active.
  void SetSynchronizedOutput(bool enable);

  // Returns whether in-band resize notifications (?2048h) are enabled.
  bool IsInBandResize() const { return in_band_resize_mode_; }

  // Sets whether in-band resize notifications (?2048h / ?2048l) are enabled.
  void SetInBandResize(bool enable) { in_band_resize_mode_ = enable; }

  // Returns whether focus in/out reporting (?1004h) is enabled.
  bool IsFocusReporting() const { return focus_reporting_mode_; }

  // Sets whether focus in/out reporting (?1004h / ?1004l) is enabled.
  void SetFocusReporting(bool enable) { focus_reporting_mode_ = enable; }

  // Returns the active mouse tracking mode (0, 1000, 1002, or 1003).
  int MouseTrackingMode() const { return mouse_tracking_mode_; }

  // Sets the active mouse tracking mode.
  void SetMouseTrackingMode(int mode) { mouse_tracking_mode_ = mode; }

  // Returns the active mouse coordinate encoding mode (0, 1006, or 1016).
  int MouseEncodingMode() const { return mouse_encoding_mode_; }

  // Sets the active mouse coordinate encoding mode.
  void SetMouseEncodingMode(int mode) { mouse_encoding_mode_ = mode; }

  // Returns whether the DEC Special Graphics box-drawing charset is active.
  bool IsDecSpecialGraphics() const { return dec_special_graphics_; }

  // Sets whether the DEC Special Graphics box-drawing charset is active.
  void SetDecSpecialGraphics(bool enable) { dec_special_graphics_ = enable; }

  // Marks a single screen row as needing repaint.
  void MarkRowDirty(int row);

  // Marks the entire viewport as needing repaint.
  void MarkAllDirty();

  // Retrieves and resets the dirty row range since the last frame.
  bool ConsumeDirtyRows(int& min_row, int& max_row, bool& full_dirty);

  // Extracts plain text from an inclusive selection range in absolute line coordinates.
  std::string ExtractText(int start_abs_line, int start_col, int end_abs_line,
                          int end_col) const;

 private:
  // Returns a mutable reference to the currently active screen vector.
  std::vector<Line>& ActiveScreen() {
    return using_alt_screen_ ? alt_screen_ : primary_screen_;
  }

  // Returns a const reference to the currently active screen vector.
  const std::vector<Line>& ActiveScreen() const {
    return using_alt_screen_ ? alt_screen_ : primary_screen_;
  }

  // Creates a blank cell using the current pen's background color.
  Cell MakeBlankCell() const;

  // Creates a blank line of `cols` cells.
  Line MakeBlankLine(int cols) const;

  // Scrolls a sub-region `[top, bottom]` up by 1 line.
  void ScrollRegionUpOneLine(int top, int bottom);

  // Scrolls a sub-region `[top, bottom]` down by 1 line.
  void ScrollRegionDownOneLine(int top, int bottom);

  // Reflows the primary screen and scrollback history when `new_cols != cols_`.
  void ReflowPrimaryScreen(int new_rows, int new_cols);

  // Initializes the default 16-color ANSI palette on `theme_`.
  void InitializeDefaultTheme();

  int rows_;
  int cols_;
  int cursor_row_;
  int cursor_col_;
  bool wrap_pending_;
  int scroll_top_;
  int scroll_bottom_;

  std::vector<Line> primary_screen_;
  std::vector<Line> alt_screen_;
  std::deque<Line> scrollback_;
  int64 base_line_index_;
  int scroll_offset_lines_;
  bool using_alt_screen_;

  Cell current_pen_;
  TerminalTheme active_theme_;
  uint32 theme_default_fg_;
  uint32 theme_default_bg_;
  uint32 theme_default_cursor_;
  uint32 selection_highlight_color_;
  TerminalThemeState theme_;
  std::vector<TerminalThemeState> color_stack_;

  SavedCursorState saved_primary_cursor_;
  SavedCursorState saved_alt_cursor_;
  SavedCursorState alt_screen_saved_cursor_;

  std::vector<std::string> hyperlinks_;
  std::vector<TerminalGraphicPlacement> graphic_placements_;
  std::vector<Line> sync_snapshot_lines_;
  std::vector<TerminalGraphicPlacement> sync_snapshot_graphics_;

  bool cursor_visible_;
  bool cursor_blink_;
  int cursor_style_;
  bool auto_wrap_mode_;
  bool origin_mode_;
  bool application_cursor_keys_;
  bool bracketed_paste_mode_;
  bool synchronized_output_;
  bool in_band_resize_mode_;
  bool focus_reporting_mode_;
  int mouse_tracking_mode_;
  int mouse_encoding_mode_;
  bool dec_special_graphics_;

  int min_dirty_row_;
  int max_dirty_row_;
  bool full_dirty_;
};
