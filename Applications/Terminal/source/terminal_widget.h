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

#include "escape_parser.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "kitty_graphics.h"
#include "kitty_input.h"
#include "perception/terminal_service.h"
#include "perception/type_id.h"
#include "perception/ui/components/focusable.h"
#include "perception/ui/components/scroll_bar.h"
#include "perception/ui/components/tooltip.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/ui/size.h"
#include "perception/window/keyboard_key_event.h"
#include "perception/window/mouse_button.h"
#include "terminal_buffer.h"

// Custom Perception UI component that renders a modern VT500/xterm/Kitty
// terminal grid, handles canonical and raw keyboard/mouse input, and manages
// scrollback and clipboard integration.
class TerminalWidget : public ::perception::UniqueIdentifiableType<TerminalWidget>,
                       public std::enable_shared_from_this<TerminalWidget> {
 public:
  // Creates a terminal widget node with optional modifiers.
  template <typename... Modifiers>
  static std::shared_ptr<::perception::ui::Node> Create(
      std::shared_ptr<::perception::ui::components::ScrollBar> scroll_bar,
      Modifiers... modifiers) {
    return ::perception::ui::Node::Empty(
        [scroll_bar](TerminalWidget& widget) {
          widget.SetScrollBar(scroll_bar);
        },
        [](::perception::ui::Layout& layout) {
          layout.SetFlexGrow(1.0f);
          layout.SetFlexShrink(1.0f);
          layout.SetMinWidth(0.0f);
          layout.SetMinHeight(0.0f);
        },
        modifiers...);
  }

  TerminalWidget();
  ~TerminalWidget();

  // Attaches this component to a Perception UI Node.
  void SetNode(std::weak_ptr<::perception::ui::Node> node);

  // Links the external vertical ScrollBar to synchronize scrollback position.
  void SetScrollBar(
      std::weak_ptr<::perception::ui::components::ScrollBar> scroll_bar);

  // Feeds raw UTF-8 / ANSI escape sequence bytes from the child process.
  void FeedOutput(std::string_view data);

  // Sets the callback invoked when the terminal sends input bytes to the PTY.
  void SetSendInputCallback(std::function<void(std::string_view)> callback);

  // Sets the callback invoked when OSC 0 / OSC 2 updates the window title.
  void SetOnTitleChangedCallback(
      std::function<void(std::string_view)> callback);

  // Sets the callback invoked when OSC 11 updates the default background color.
  void SetOnBackgroundColorChangedCallback(
      std::function<void(uint32)> callback);

  // Sets the callback invoked when the terminal grid dimensions change.
  void SetOnResizeCallback(
      std::function<void(int rows, int cols, int width_px, int height_px)>
          callback);

  // Sets the callback invoked when the mouse cursor shape changes via OSC 22 or hover.
  void SetOnCursorChangedCallback(
      std::function<void(::perception::window::Cursor)> callback);

  // Sets the getter used to query termios flags (such as ICANON and ECHO).
  void SetTerminalAttributesGetter(
      std::function<::perception::TerminalAttributes()> getter);

  // Notifies the terminal widget that the parent window focus changed.
  void NotifyWindowFocusChanged(bool focused);

  // Copies the active text selection to the system clipboard.
  void CopySelection();

  // Pastes text from the system clipboard into the terminal input stream.
  void PasteClipboard();

  // Selects all lines in scrollback and the active screen.
  void SelectAll();

  // Clears the scrollback history buffer.
  void ClearScrollback();

  // Applies the specified terminal color theme (Dark or Light).
  void ApplyTheme(TerminalTheme theme);

  // Returns the currently active terminal color theme.
  TerminalTheme GetTheme() const { return buffer_.GetTheme(); }

  // Returns the active default background color of the terminal buffer.
  uint32 GetDefaultBackgroundColor() const {
    return buffer_.GetDefaultBackgroundColor();
  }

  // Returns the current number of visible rows.
  int GetRows() const { return buffer_.Rows(); }

  // Returns the current number of visible columns.
  int GetCols() const { return buffer_.Cols(); }

  // Returns the measured width of a single character cell in pixels.
  float GetCellWidth() const { return cell_width_; }

  // Returns the measured height of a single character cell in pixels.
  float GetCellHeight() const { return cell_height_; }

 private:
  // Ensures font metrics have been measured for the cell grid.
  void UpdateFontMetrics();

  // Updates the grid dimensions if the widget layout size changed.
  void UpdateGridSizeIfNeeded(float width, float height);

  // Synchronizes the vertical ScrollBar range and thumb position.
  void SyncScrollBar();

  // Invalidates dirty rows in the UI node.
  void InvalidateDirtyRows();

  // Sends raw bytes to the child process stdin pipe.
  void SendToPty(std::string_view data);

  // Handles user input text (respecting canonical mode line buffering and echo).
  void ProcessUserInputBytes(std::string_view bytes);

  // Draws the 6-pass terminal surface.
  void Draw(const ::perception::ui::DrawContext& draw_context);

  // Measures the preferred initial size of the terminal widget.
  ::perception::ui::Size Measure(float width, YGMeasureMode width_mode,
                                 float height, YGMeasureMode height_mode);

  // Draws custom pixel-aligned Box Drawing, Block Element, and Braille glyphs.
  bool DrawCustomGlyph(SkCanvas& canvas, uint32_t codepoint, float x, float y,
                       float w, float h, uint32_t fg_color);

  // Converts a widget-local pixel coordinate to a clamped (row, col) pair.
  void PointToCell(const ::perception::ui::Point& point, int& row,
                   int& col) const;

  // Converts a visible row index to an absolute line index in the buffer.
  size_t VisibleRowToAbsoluteLine(int visible_row) const;

  // Returns normalized selection bounds in (abs_line, col) order.
  bool GetNormalizedSelection(size_t& start_line, int& start_col,
                              size_t& end_line, int& end_col) const;

  // Clears the current text selection.
  void ClearSelection();

  // Updates the mouse cursor and tooltip when hovering over cells.
  void UpdateCursorAndHoverAt(const ::perception::ui::Point& point);

  // Starts the 500ms cursor and text blink timer.
  void StartBlinkTimer();

  // Resets the cursor blink phase to visible when typing or receiving output.
  void ResetCursorBlinkPhase();

  // Shows the right-click context menu at the given widget-local point.
  void ShowContextMenu(const ::perception::ui::Point& point);

  // Keyboard event handlers.
  void HandleKeyDown(const ::perception::window::KeyboardKeyEvent& event);
  void HandleKeyUp(const ::perception::window::KeyboardKeyEvent& event);

  // Mouse event handlers.
  void HandleMouseHover(const ::perception::ui::Point& point);
  void HandleMouseLeave();
  void HandleMouseButtonDown(const ::perception::ui::Point& point,
                             ::perception::window::MouseButton button);
  void HandleMouseButtonUp(const ::perception::ui::Point& point,
                           ::perception::window::MouseButton button);
  ::perception::ui::Point HandleMouseScroll(
      const ::perception::ui::Point& point,
      const ::perception::ui::Point& delta);

  std::weak_ptr<::perception::ui::Node> node_;
  std::shared_ptr<::perception::ui::components::Focusable> focusable_;
  std::weak_ptr<::perception::ui::components::ScrollBar> scroll_bar_;
  std::shared_ptr<::perception::ui::components::Tooltip> tooltip_;

  TerminalBuffer buffer_;
  KittyGraphics kitty_graphics_;
  KittyInput kitty_input_;
  std::unique_ptr<EscapeParser> escape_parser_;

  float cell_width_;
  float cell_height_;
  float cell_baseline_;
  bool metrics_initialized_;

  bool shift_pressed_;
  bool ctrl_pressed_;
  bool alt_pressed_;
  bool super_pressed_;
  bool window_focused_;

  bool is_selecting_;
  bool has_selection_;
  size_t selection_anchor_line_;
  int selection_anchor_col_;
  size_t selection_end_line_;
  int selection_end_col_;

  uint32_t hovered_hyperlink_id_;
  ::perception::ui::Point last_mouse_point_;
  ::perception::window::MouseButton active_mouse_button_;
  ::perception::window::Cursor last_applied_cursor_;

  bool cursor_blink_phase_on_;
  bool blink_timer_running_;
  std::shared_ptr<bool> alive_token_;

  std::string canonical_line_buffer_;

  std::function<void(std::string_view)> send_input_callback_;
  std::function<void(std::string_view)> on_title_changed_callback_;
  std::function<void(uint32)> on_background_color_changed_callback_;
  std::function<void(int, int, int, int)> on_resize_callback_;
  std::function<void(::perception::window::Cursor)> on_cursor_changed_callback_;
  std::function<::perception::TerminalAttributes()> terminal_attributes_getter_;
};

namespace perception {
extern template class UniqueIdentifiableType<::TerminalWidget>;
}  // namespace perception
