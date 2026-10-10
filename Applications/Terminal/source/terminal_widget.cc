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

#include "terminal_widget.h"

#include <algorithm>
#include <cmath>

#include "include/core/SkColor.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "perception/clipboard.h"
#include "perception/time.h"
#include "perception/ui/components/pop_up.h"
#include "perception/ui/font.h"
#include "perception/ui/keyboard.h"
#include "perception/ui/text_handling.h"
#include "perception/ui/theme.h"

namespace perception {
template class UniqueIdentifiableType<::TerminalWidget>;
}  // namespace perception

namespace {

// Base font size in points for standard scale-1 terminal cells.
constexpr float kDefaultFontSize = 13.0f;

// Fallback cell width in pixels when font metrics are unavailable.
constexpr float kFallbackCellWidth = 8.0f;

// Fallback cell height in pixels when font metrics are unavailable.
constexpr float kFallbackCellHeight = 16.0f;

// Fallback baseline offset from top of cell in pixels.
constexpr float kFallbackCellBaseline = 12.0f;

// Stroke width for single/double/dotted/dashed underlines and strikethroughs.
constexpr float kDecorationThickness = 1.0f;

// Thickness in pixels for underline and vertical bar cursors.
constexpr float kCursorThickness = 2.0f;

// Stroke width in pixels for the hollow unfocused block cursor.
constexpr float kUnfocusedCursorStrokeWidth = 1.0f;

//Sine wave amplitude in pixels for curly (undercurl) underlines.
constexpr float kCurlyWaveAmplitude = 1.5f;

// Wavelength in pixels for curly (undercurl) underlines.
constexpr float kCurlyWavePeriod = 6.0f;

// Dash length in pixels for dashed underlines.
constexpr float kDashLength = 3.0f;

// Gap length in pixels for dashed underlines.
constexpr float kDashGap = 2.0f;

// Dot spacing in pixels for dotted underlines.
constexpr float kDotSpacing = 2.0f;

// Half-period interval in milliseconds for cursor and SGR 5 text blinking.
constexpr int kCursorBlinkIntervalMs = 500;

// POSIX termios ICANON local flag bitmask.
constexpr uint32 kTermiosIcanonFlag = 0x0002u;

// POSIX termios ECHO local flag bitmask.
constexpr uint32 kTermiosEchoFlag = 0x0008u;

// ASCII End-of-Text (Ctrl+C / SIGINT) character code.
constexpr char kAsciiEtx = 0x03;

// ASCII End-of-Transmission (Ctrl+D / EOF) character code.
constexpr char kAsciiEot = 0x04;

// SGR underline style: no underline.
constexpr uint8 kUnderlineNone = 0;

// SGR underline style: single straight line.
constexpr uint8 kUnderlineSingle = 1;

// SGR underline style: double straight line.
constexpr uint8 kUnderlineDouble = 2;

// SGR underline style: curly wave (undercurl).
constexpr uint8 kUnderlineCurly = 3;

// SGR underline style: dotted line.
constexpr uint8 kUnderlineDotted = 4;

// SGR underline style: dashed line.
constexpr uint8 kUnderlineDashed = 5;

// Encodes a single Unicode codepoint into a UTF-8 byte sequence.
size_t EncodeUtf8Codepoint(char32_t cp, char out[4]) {
  if (cp <= 0x7F) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp <= 0x7FF) {
    out[0] = static_cast<char>(0xC0 | ((cp >> 6) & 0x1F));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp <= 0xFFFF) {
    out[0] = static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  }
  if (cp <= 0x10FFFF) {
    out[0] = static_cast<char>(0xF0 | ((cp >> 18) & 0x07));
    out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[3] = static_cast<char>(0x80 | (cp & 0x3F));
    return 4;
  }
  out[0] = '?';
  return 1;
}

// Applies SGR 2 dimming by blending the foreground color 50% toward the background.
uint32 ApplyDimColor(uint32 fg, uint32 bg) {
  uint32 r = (((fg >> 16) & 0xFF) + ((bg >> 16) & 0xFF)) / 2;
  uint32 g = (((fg >> 8) & 0xFF) + ((bg >> 8) & 0xFF)) / 2;
  uint32 b = ((fg & 0xFF) + (bg & 0xFF)) / 2;
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}

}  // namespace

using ::perception::TerminalAttributes;
using ::perception::ui::DrawContext;
using ::perception::ui::GetMonaspaceUiFont;
using ::perception::ui::IsControlKey;
using ::perception::ui::IsShiftKey;
using ::perception::ui::KeyCode;
using ::perception::ui::MonaspaceFamily;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::Rectangle;
using ::perception::ui::ScancodeToAscii;
using ::perception::ui::Size;
using ::perception::ui::components::Focusable;
using ::perception::ui::components::PopUp;
using ::perception::ui::components::PopUpMenu;
using ::perception::ui::components::ScrollBar;
using ::perception::ui::components::Tooltip;
using ::perception::window::Cursor;
using ::perception::window::KeyboardKeyEvent;
using ::perception::window::MouseButton;

TerminalWidget::TerminalWidget()
    : buffer_(kDefaultTerminalRows, kDefaultTerminalCols),
      cell_width_(kFallbackCellWidth),
      cell_height_(kFallbackCellHeight),
      cell_baseline_(kFallbackCellBaseline),
      metrics_initialized_(false),
      shift_pressed_(false),
      ctrl_pressed_(false),
      alt_pressed_(false),
      super_pressed_(false),
      window_focused_(true),
      is_selecting_(false),
      has_selection_(false),
      selection_anchor_line_(0),
      selection_anchor_col_(0),
      selection_end_line_(0),
      selection_end_col_(0),
      hovered_hyperlink_id_(0),
      last_mouse_point_{0.0f, 0.0f},
      active_mouse_button_(MouseButton::Unknown),
      last_applied_cursor_(Cursor::Caret),
      cursor_blink_phase_on_(true),
      blink_timer_running_(false),
      alive_token_(std::make_shared<bool>(true)) {
  EscapeParserCallbacks callbacks;
  callbacks.send_response = [this](std::string_view resp) { SendToPty(resp); };
  callbacks.on_title_changed = [this](std::string_view title) {
    if (on_title_changed_callback_)
      on_title_changed_callback_(title);
  };
  callbacks.on_background_color_changed = [this](uint32 color) {
    if (on_background_color_changed_callback_)
      on_background_color_changed_callback_(color);
  };
  callbacks.on_cursor_shape_changed = [this]() {
    ResetCursorBlinkPhase();
    UpdateCursorAndHoverAt(last_mouse_point_);
    InvalidateDirtyRows();
  };
  callbacks.on_set_clipboard = [](std::string_view text) {
    ::perception::SetClipboard(text);
  };
  callbacks.on_get_clipboard = []() -> std::string {
    auto status_or_val = ::perception::GetClipboard();
    if (status_or_val.Ok())
      return status_or_val->ToString();
    return "";
  };
  callbacks.on_sync_output_ended = [this]() {
    SyncScrollBar();
    InvalidateDirtyRows();
  };

  escape_parser_ = std::make_unique<EscapeParser>(buffer_, kitty_graphics_,
                                                  kitty_input_, callbacks);
}

TerminalWidget::~TerminalWidget() {
  if (alive_token_)
    *alive_token_ = false;
}

void TerminalWidget::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  if (node_.expired())
    return;
  auto strong_node = node_.lock();

  UpdateFontMetrics();

  strong_node->SetBlocksHitTest(true);
  strong_node->SetCursor(Cursor::Caret);
  strong_node->OnDraw(std::bind_front(&TerminalWidget::Draw, this));
  strong_node->SetMeasureFunction(
      std::bind_front(&TerminalWidget::Measure, this));
  strong_node->OnMouseHover(
      std::bind_front(&TerminalWidget::HandleMouseHover, this));
  strong_node->OnMouseLeave(
      std::bind_front(&TerminalWidget::HandleMouseLeave, this));
  strong_node->OnMouseButtonDown(
      std::bind_front(&TerminalWidget::HandleMouseButtonDown, this));
  strong_node->OnMouseButtonUp(
      std::bind_front(&TerminalWidget::HandleMouseButtonUp, this));
  strong_node->OnMouseScroll(
      std::bind_front(&TerminalWidget::HandleMouseScroll, this));

  tooltip_ = std::make_shared<Tooltip>();
  tooltip_->SetNode(strong_node);

  focusable_ = strong_node->GetOrAdd<Focusable>();
  focusable_->OnFocus([this]() {
    ResetCursorBlinkPhase();
    buffer_.MarkRowDirty(buffer_.CursorRow());
    InvalidateDirtyRows();
  });
  focusable_->OnUnfocus([this]() {
    shift_pressed_ = false;
    ctrl_pressed_ = false;
    alt_pressed_ = false;
    super_pressed_ = false;
    buffer_.MarkRowDirty(buffer_.CursorRow());
    InvalidateDirtyRows();
  });
  focusable_->OnKeyDown(
      std::bind_front(&TerminalWidget::HandleKeyDown, this));
  focusable_->OnKeyUp(std::bind_front(&TerminalWidget::HandleKeyUp, this));
  focusable_->Focus();

  StartBlinkTimer();
}

void TerminalWidget::SetScrollBar(std::weak_ptr<ScrollBar> scroll_bar) {
  scroll_bar_ = scroll_bar;
  auto strong_bar = scroll_bar_.lock();
  if (!strong_bar)
    return;

  strong_bar->OnScroll([this](float value) {
    if (focusable_ && window_focused_ && !focusable_->HasFocus())
      focusable_->Focus();
    float row_height =
        (cell_height_ > 0.0f) ? cell_height_ : kFallbackCellHeight;
    int scrollback = buffer_.ScrollbackSize();
    int line_value = static_cast<int>(std::round(value / row_height));
    int offset = std::clamp(scrollback - line_value, 0, scrollback);
    if (offset != buffer_.ScrollOffsetLines()) {
      buffer_.SetScrollOffsetLines(offset);
      InvalidateDirtyRows();
    }
  });
  SyncScrollBar();
}

void TerminalWidget::FeedOutput(std::string_view data) {
  if (data.empty())
    return;
  UpdateFontMetrics();
  ResetCursorBlinkPhase();
  escape_parser_->Feed(data);
  if (!buffer_.IsSynchronizedOutput()) {
    SyncScrollBar();
    InvalidateDirtyRows();
  }
}

void TerminalWidget::SetSendInputCallback(
    std::function<void(std::string_view)> callback) {
  send_input_callback_ = std::move(callback);
}

void TerminalWidget::SetOnTitleChangedCallback(
    std::function<void(std::string_view)> callback) {
  on_title_changed_callback_ = std::move(callback);
}

void TerminalWidget::SetOnBackgroundColorChangedCallback(
    std::function<void(uint32)> callback) {
  on_background_color_changed_callback_ = std::move(callback);
}

void TerminalWidget::SetOnResizeCallback(
    std::function<void(int rows, int cols, int width_px, int height_px)>
        callback) {
  on_resize_callback_ = std::move(callback);
}

void TerminalWidget::SetOnCursorChangedCallback(
    std::function<void(Cursor)> callback) {
  on_cursor_changed_callback_ = std::move(callback);
}

void TerminalWidget::SetTerminalAttributesGetter(
    std::function<TerminalAttributes()> getter) {
  terminal_attributes_getter_ = std::move(getter);
}

void TerminalWidget::NotifyWindowFocusChanged(bool focused) {
  if (window_focused_ == focused)
    return;
  window_focused_ = focused;
  if (buffer_.IsFocusReporting())
    SendToPty(focused ? "\x1b[I" : "\x1b[O");
  buffer_.MarkRowDirty(buffer_.CursorRow());
  InvalidateDirtyRows();
}

void TerminalWidget::CopySelection() {
  size_t start_line = 0;
  int start_col = 0;
  size_t end_line = 0;
  int end_col = 0;
  if (!GetNormalizedSelection(start_line, start_col, end_line, end_col))
    return;
  std::string text = buffer_.ExtractText(
      static_cast<int>(start_line), start_col, static_cast<int>(end_line),
      end_col);
  if (!text.empty())
    ::perception::SetClipboard(text);
}

void TerminalWidget::PasteClipboard() {
  auto status_or_val = ::perception::GetClipboard();
  if (!status_or_val.Ok())
    return;
  std::string text = status_or_val->ToString();
  if (text.empty())
    return;

  if (buffer_.ScrollOffsetLines() > 0) {
    buffer_.SetScrollOffsetLines(0);
    SyncScrollBar();
  }

  if (buffer_.IsBracketedPaste()) {
    std::string sanitized;
    sanitized.reserve(text.size() + 16);
    sanitized.append("\x1b[200~");
    for (size_t i = 0; i < text.size(); ++i) {
      if (i + 5 < text.size() && text.substr(i, 6) == "\x1b[201~") {
        i += 5;
        continue;
      }
      sanitized.push_back(text[i]);
    }
    sanitized.append("\x1b[201~");
    SendToPty(sanitized);
  } else {
    ProcessUserInputBytes(text);
  }
}

void TerminalWidget::SelectAll() {
  int total = buffer_.TotalLines();
  if (total <= 0)
    return;
  has_selection_ = true;
  selection_anchor_line_ = 0;
  selection_anchor_col_ = 0;
  selection_end_line_ = static_cast<size_t>(total - 1);
  selection_end_col_ = std::max(0, buffer_.Cols() - 1);
  buffer_.MarkAllDirty();
  InvalidateDirtyRows();
}

void TerminalWidget::ClearScrollback() {
  buffer_.ClearScrollback();
  ClearSelection();
  SyncScrollBar();
  InvalidateDirtyRows();
}

void TerminalWidget::ApplyTheme(TerminalTheme theme) {
  buffer_.ApplyTheme(theme);
  if (on_background_color_changed_callback_)
    on_background_color_changed_callback_(buffer_.GetDefaultBackgroundColor());
  InvalidateDirtyRows();
}

void TerminalWidget::UpdateFontMetrics() {
  if (metrics_initialized_)
    return;
  SkFont* font =
      GetMonaspaceUiFont(MonaspaceFamily::Neon, kDefaultFontSize, false, false);
  if (!font)
    return;

  float advance = font->measureText("M", 1, SkTextEncoding::kUTF8);
  SkFontMetrics fm;
  float spacing = font->getMetrics(&fm);

  if (advance > 1.0f)
    cell_width_ = std::ceil(advance);
  if (spacing > 1.0f) {
    cell_height_ = std::ceil(spacing);
    cell_baseline_ = std::ceil(-fm.fAscent);
  }
  escape_parser_->SetCellMetrics(static_cast<int>(cell_width_),
                                 static_cast<int>(cell_height_));
  metrics_initialized_ = true;
}

void TerminalWidget::UpdateGridSizeIfNeeded(float width, float height) {
  UpdateFontMetrics();
  if (width <= 0.0f || height <= 0.0f || cell_width_ <= 0.0f ||
      cell_height_ <= 0.0f) {
    return;
  }

  int new_cols = std::max(1, static_cast<int>(std::floor(width / cell_width_)));
  int new_rows =
      std::max(1, static_cast<int>(std::floor(height / cell_height_)));
  if (new_cols == buffer_.Cols() && new_rows == buffer_.Rows())
    return;

  buffer_.Resize(new_rows, new_cols);
  ClearSelection();
  SyncScrollBar();

  int width_px = static_cast<int>(new_cols * cell_width_);
  int height_px = static_cast<int>(new_rows * cell_height_);
  if (on_resize_callback_)
    on_resize_callback_(new_rows, new_cols, width_px, height_px);

  if (buffer_.IsInBandResize()) {
    std::string msg = "\x1b[48;" + std::to_string(new_rows) + ";" +
                      std::to_string(new_cols) + ";" +
                      std::to_string(height_px) + ";" +
                      std::to_string(width_px) + "t";
    SendToPty(msg);
  }
}

void TerminalWidget::SyncScrollBar() {
  auto strong_bar = scroll_bar_.lock();
  if (!strong_bar)
    return;

  float row_height =
      (cell_height_ > 0.0f) ? cell_height_ : kFallbackCellHeight;
  float total = static_cast<float>(buffer_.TotalLines()) * row_height;
  float visible = static_cast<float>(buffer_.Rows()) * row_height;
  float scrollback = static_cast<float>(buffer_.ScrollbackSize()) * row_height;
  float value = std::max(
      0.0f,
      scrollback - static_cast<float>(buffer_.ScrollOffsetLines()) * row_height);
  strong_bar->SetValue(0.0f, total, value, visible);
}

void TerminalWidget::InvalidateDirtyRows() {
  auto strong_node = node_.lock();
  if (!strong_node)
    return;

  int min_row = 0;
  int max_row = 0;
  bool full_dirty = false;
  if (!buffer_.ConsumeDirtyRows(min_row, max_row, full_dirty))
    return;

  Size sz = strong_node->GetSize();
  if (full_dirty || cell_height_ <= 0.0f || sz.width <= 0.0f ||
      sz.height <= 0.0f) {
    strong_node->Invalidate();
    return;
  }

  float y = static_cast<float>(min_row) * cell_height_;
  float h = static_cast<float>(max_row - min_row + 1) * cell_height_;
  strong_node->Invalidate(
      Rectangle::FromMinMaxPoints({0.0f, y}, {sz.width, y + h}));
}

void TerminalWidget::SendToPty(std::string_view data) {
  if (!data.empty() && send_input_callback_)
    send_input_callback_(data);
}

void TerminalWidget::ProcessUserInputBytes(std::string_view bytes) {
  if (bytes.empty())
    return;

  uint32 c_lflag = kTermiosIcanonFlag | kTermiosEchoFlag;
  if (terminal_attributes_getter_)
    c_lflag = terminal_attributes_getter_().c_lflag;

  bool is_canonical =
      !buffer_.IsAlternateScreen() &&
      (kitty_input_.GetKeyboardFlags(buffer_.IsAlternateScreen()) == 0) &&
      ((c_lflag & kTermiosIcanonFlag) != 0);
  bool should_echo = (c_lflag & kTermiosEchoFlag) != 0;

  if (!is_canonical) {
    if (!canonical_line_buffer_.empty()) {
      SendToPty(canonical_line_buffer_);
      canonical_line_buffer_.clear();
    }
    if (should_echo)
      FeedOutput(bytes);
    SendToPty(bytes);
    return;
  }

  for (char ch : bytes) {
    if (ch == '\r' || ch == '\n') {
      canonical_line_buffer_.push_back('\n');
      if (should_echo)
        FeedOutput("\r\n");
      SendToPty(canonical_line_buffer_);
      canonical_line_buffer_.clear();
    } else if (ch == 0x7F || ch == '\b') {
      if (!canonical_line_buffer_.empty()) {
        while (!canonical_line_buffer_.empty() &&
               (static_cast<uint8>(canonical_line_buffer_.back()) & 0xC0) ==
                   0x80) {
          canonical_line_buffer_.pop_back();
        }
        if (!canonical_line_buffer_.empty())
          canonical_line_buffer_.pop_back();
        if (should_echo)
          FeedOutput("\b \b");
      }
    } else if (ch == kAsciiEtx) {
      canonical_line_buffer_.clear();
      if (should_echo)
        FeedOutput("^C\r\n");
      const char etx = kAsciiEtx;
      SendToPty(std::string_view(&etx, 1));
    } else if (ch == kAsciiEot) {
      if (!canonical_line_buffer_.empty()) {
        SendToPty(canonical_line_buffer_);
        canonical_line_buffer_.clear();
      } else {
        const char eot = kAsciiEot;
        SendToPty(std::string_view(&eot, 1));
      }
    } else if (static_cast<uint8>(ch) >= 0x20 || ch == '\t') {
      canonical_line_buffer_.push_back(ch);
      if (should_echo)
        FeedOutput(std::string_view(&ch, 1));
    }
  }
}

void TerminalWidget::Draw(const DrawContext& draw_context) {
  if (!draw_context.skia_canvas)
    return;

  UpdateGridSizeIfNeeded(draw_context.area.Width(), draw_context.area.Height());

  SkCanvas& canvas = *draw_context.skia_canvas;
  canvas.save();
  float origin_x = draw_context.area.MinX();
  float origin_y = draw_context.area.MinY();
  canvas.clipRect(SkRect::MakeXYWH(origin_x, origin_y,
                                   draw_context.area.Width(),
                                   draw_context.area.Height()));

  int rows = buffer_.Rows();
  int cols = buffer_.Cols();
  uint32 default_bg = buffer_.GetDefaultBackgroundColor();

  // Pass 1: Background fills with horizontal run-length merging.
  SkPaint bg_paint;
  bg_paint.setStyle(SkPaint::kFill_Style);
  bg_paint.setAntiAlias(false);
  bg_paint.setColor(default_bg);
  canvas.drawRect(SkRect::MakeXYWH(origin_x, origin_y,
                                   draw_context.area.Width(),
                                   draw_context.area.Height()),
                  bg_paint);

  for (int r = 0; r < rows; ++r) {
    const Line& line = buffer_.GetVisibleLine(r);
    int line_cols = std::min(cols, static_cast<int>(line.cells.size()));
    int run_start = -1;
    uint32 run_color = default_bg;

    for (int c = 0; c < line_cols; ++c) {
      const Cell& cell = line.cells[c];
      uint32 eff_bg = (cell.flags & kCellFlagInverse) ? cell.fg_color
                                                      : cell.bg_color;
      if (eff_bg != run_color) {
        if (run_start >= 0 && run_color != default_bg) {
          bg_paint.setColor(run_color);
          canvas.drawRect(
              SkRect::MakeXYWH(origin_x + run_start * cell_width_,
                               origin_y + r * cell_height_,
                               (c - run_start) * cell_width_, cell_height_),
              bg_paint);
        }
        run_start = c;
        run_color = eff_bg;
      }
    }
    if (run_start >= 0 && run_color != default_bg) {
      bg_paint.setColor(run_color);
      canvas.drawRect(
          SkRect::MakeXYWH(origin_x + run_start * cell_width_,
                           origin_y + r * cell_height_,
                           (line_cols - run_start) * cell_width_, cell_height_),
          bg_paint);
    }
  }

  // Pass 2: Below-text Kitty and Sixel graphic placements (z_index < 0).
  int64 top_line_id = buffer_.TopVisibleLineId();
  bool is_alt = buffer_.IsAlternateScreen();
  for (const auto& placement : buffer_.GetGraphicPlacements()) {
    if (placement.is_alt_screen != is_alt || placement.z_index >= 0 ||
        !placement.image) {
      continue;
    }
    int vis_row = static_cast<int>(placement.anchor_line_id - top_line_id);
    float dst_x =
        origin_x + placement.anchor_col * cell_width_ + placement.x_offset;
    float dst_y = origin_y + vis_row * cell_height_ + placement.y_offset;
    float dst_w = placement.display_cols > 0
                      ? placement.display_cols * cell_width_
                      : static_cast<float>(placement.image->width());
    float dst_h = placement.display_rows > 0
                      ? placement.display_rows * cell_height_
                      : static_cast<float>(placement.image->height());
    if (dst_y + dst_h < origin_y ||
        dst_y > origin_y + draw_context.area.Height()) {
      continue;
    }
    SkRect src_rect = SkRect::MakeXYWH(
        static_cast<float>(placement.crop_x),
        static_cast<float>(placement.crop_y),
        placement.crop_w > 0 ? static_cast<float>(placement.crop_w)
                             : static_cast<float>(placement.image->width()),
        placement.crop_h > 0 ? static_cast<float>(placement.crop_h)
                             : static_cast<float>(placement.image->height()));
    SkRect dst_rect = SkRect::MakeXYWH(dst_x, dst_y, dst_w, dst_h);
    canvas.drawImageRect(placement.image, src_rect, dst_rect,
                         SkSamplingOptions(SkFilterMode::kLinear), nullptr,
                         SkCanvas::kStrict_SrcRectConstraint);
  }

  // Pass 3: Selection highlight overlay.
  size_t sel_start_line = 0;
  int sel_start_col = 0;
  size_t sel_end_line = 0;
  int sel_end_col = 0;
  if (GetNormalizedSelection(sel_start_line, sel_start_col, sel_end_line,
                             sel_end_col)) {
    SkPaint sel_paint;
    sel_paint.setStyle(SkPaint::kFill_Style);
    sel_paint.setColor(buffer_.GetSelectionHighlightColor());
    for (int r = 0; r < rows; ++r) {
      size_t abs_line = VisibleRowToAbsoluteLine(r);
      if (abs_line < sel_start_line || abs_line > sel_end_line)
        continue;
      int c0 = (abs_line == sel_start_line) ? sel_start_col : 0;
      int c1 = (abs_line == sel_end_line) ? sel_end_col : (cols - 1);
      c0 = std::clamp(c0, 0, cols - 1);
      c1 = std::clamp(c1, 0, cols - 1);
      if (c0 <= c1)
        canvas.drawRect(
            SkRect::MakeXYWH(origin_x + c0 * cell_width_,
                             origin_y + r * cell_height_,
                             (c1 - c0 + 1) * cell_width_, cell_height_),
            sel_paint);
    }
  }

  // Pass 4: Glyphs, custom Box/Block/Braille geometry, and OSC 66 scaled text.
  SkPaint fg_paint;
  fg_paint.setAntiAlias(true);
  for (int r = 0; r < rows; ++r) {
    const Line& line = buffer_.GetVisibleLine(r);
    int line_cols = std::min(cols, static_cast<int>(line.cells.size()));
    float cell_y = origin_y + r * cell_height_;

    for (int c = 0; c < line_cols; ++c) {
      const Cell& cell = line.cells[c];
      if (cell.cell_width == 0 || (cell.flags & kCellFlagHidden) ||
          ((cell.flags & kCellFlagBlink) && !cursor_blink_phase_on_) ||
          cell.codepoint <= U' ') {
        continue;
      }

      uint32 eff_fg = (cell.flags & kCellFlagInverse) ? cell.bg_color
                                                      : cell.fg_color;
      uint32 eff_bg = (cell.flags & kCellFlagInverse) ? cell.fg_color
                                                      : cell.bg_color;
      if (cell.flags & kCellFlagDim)
        eff_fg = ApplyDimColor(eff_fg, eff_bg);

      float cell_x = origin_x + c * cell_width_;
      int scale = std::clamp<int>(cell.scale, 1, 7);
      float glyph_w = cell_width_ * std::max<int>(1, cell.cell_width);
      float glyph_h = cell_height_ * scale;

      if (scale == 1 &&
          DrawCustomGlyph(canvas, cell.codepoint, cell_x, cell_y, glyph_w,
                          glyph_h, eff_fg)) {
        continue;
      }

      MonaspaceFamily family = static_cast<MonaspaceFamily>(
          std::min<uint8>(cell.font_family, 4));
      bool bold = (cell.flags & kCellFlagBold) != 0;
      bool italic = (cell.flags & kCellFlagItalic) != 0;
      SkFont* font = GetMonaspaceUiFont(family, kDefaultFontSize * scale, bold,
                                        italic);
      if (!font)
        continue;

      char utf8[4];
      size_t utf8_len = EncodeUtf8Codepoint(cell.codepoint, utf8);
      fg_paint.setColor(eff_fg);
      float baseline_y = cell_y + cell_baseline_ * scale;
      canvas.drawSimpleText(utf8, utf8_len, SkTextEncoding::kUTF8, cell_x,
                            baseline_y, *font, fg_paint);
    }
  }

  // Pass 5: Underlines (single, double, curly, dotted, dashed), hyperlinks, and strikethroughs.
  SkPaint dec_paint;
  dec_paint.setAntiAlias(true);
  for (int r = 0; r < rows; ++r) {
    const Line& line = buffer_.GetVisibleLine(r);
    int line_cols = std::min(cols, static_cast<int>(line.cells.size()));
    float cell_y = origin_y + r * cell_height_;

    for (int c = 0; c < line_cols; ++c) {
      const Cell& cell = line.cells[c];
      if (cell.cell_width == 0)
        continue;

      uint8 u_style = cell.underline_style;
      if (u_style == kUnderlineNone && cell.hyperlink_id != 0) {
        u_style = (cell.hyperlink_id == hovered_hyperlink_id_)
                      ? kUnderlineSingle
                      : kUnderlineDotted;
      }
      bool strike = (cell.flags & kCellFlagStrikethrough) != 0;
      if (u_style == kUnderlineNone && !strike)
        continue;

      uint32 eff_fg = (cell.flags & kCellFlagInverse) ? cell.bg_color
                                                      : cell.fg_color;
      uint32 u_color = cell.underline_color != 0 ? cell.underline_color : eff_fg;
      float x0 = origin_x + c * cell_width_;
      float x1 = x0 + cell_width_ * std::max<int>(1, cell.cell_width);
      float uy = cell_y + cell_height_ - 2.0f;

      if (u_style != kUnderlineNone) {
        dec_paint.setColor(u_color);
        dec_paint.setStrokeWidth(kDecorationThickness);
        if (u_style == kUnderlineSingle) {
          dec_paint.setStyle(SkPaint::kStroke_Style);
          canvas.drawLine(x0, uy, x1, uy, dec_paint);
        } else if (u_style == kUnderlineDouble) {
          dec_paint.setStyle(SkPaint::kStroke_Style);
          canvas.drawLine(x0, uy - 2.0f, x1, uy - 2.0f, dec_paint);
          canvas.drawLine(x0, uy, x1, uy, dec_paint);
        } else if (u_style == kUnderlineCurly) {
          dec_paint.setStyle(SkPaint::kStroke_Style);
          SkPathBuilder path_builder;
          path_builder.moveTo(x0, uy);
          for (float x = x0; x < x1; x += 1.0f) {
            float phase =
                ((x - origin_x) / kCurlyWavePeriod) * 2.0f * 3.14159265f;
            path_builder.lineTo(x + 1.0f,
                                uy + std::sin(phase) * kCurlyWaveAmplitude);
          }
          canvas.drawPath(path_builder.detach(), dec_paint);
        } else if (u_style == kUnderlineDotted) {
          dec_paint.setStyle(SkPaint::kFill_Style);
          for (float x = x0; x < x1; x += kDotSpacing) {
            canvas.drawRect(
                SkRect::MakeXYWH(x, uy - 0.5f, 1.0f, kDecorationThickness),
                dec_paint);
          }
        } else if (u_style == kUnderlineDashed) {
          dec_paint.setStyle(SkPaint::kStroke_Style);
          for (float x = x0; x < x1; x += (kDashLength + kDashGap)) {
            canvas.drawLine(x, uy, std::min(x + kDashLength, x1), uy,
                            dec_paint);
          }
        }
      }

      if (strike) {
        dec_paint.setColor(eff_fg);
        dec_paint.setStyle(SkPaint::kStroke_Style);
        dec_paint.setStrokeWidth(kDecorationThickness);
        float sy = cell_y + cell_height_ * 0.5f;
        canvas.drawLine(x0, sy, x1, sy, dec_paint);
      }
    }
  }

  // Pass 6: Above-text Kitty/Sixel graphics (z_index >= 0) and active cursor.
  for (const auto& placement : buffer_.GetGraphicPlacements()) {
    if (placement.is_alt_screen != is_alt || placement.z_index < 0 ||
        !placement.image) {
      continue;
    }
    int vis_row = static_cast<int>(placement.anchor_line_id - top_line_id);
    float dst_x =
        origin_x + placement.anchor_col * cell_width_ + placement.x_offset;
    float dst_y = origin_y + vis_row * cell_height_ + placement.y_offset;
    float dst_w = placement.display_cols > 0
                      ? placement.display_cols * cell_width_
                      : static_cast<float>(placement.image->width());
    float dst_h = placement.display_rows > 0
                      ? placement.display_rows * cell_height_
                      : static_cast<float>(placement.image->height());
    if (dst_y + dst_h < origin_y ||
        dst_y > origin_y + draw_context.area.Height()) {
      continue;
    }
    SkRect src_rect = SkRect::MakeXYWH(
        static_cast<float>(placement.crop_x),
        static_cast<float>(placement.crop_y),
        placement.crop_w > 0 ? static_cast<float>(placement.crop_w)
                             : static_cast<float>(placement.image->width()),
        placement.crop_h > 0 ? static_cast<float>(placement.crop_h)
                             : static_cast<float>(placement.image->height()));
    SkRect dst_rect = SkRect::MakeXYWH(dst_x, dst_y, dst_w, dst_h);
    canvas.drawImageRect(placement.image, src_rect, dst_rect,
                         SkSamplingOptions(SkFilterMode::kLinear), nullptr,
                         SkCanvas::kStrict_SrcRectConstraint);
  }

  if (buffer_.IsCursorVisible() && buffer_.ScrollOffsetLines() == 0) {
    int crow = buffer_.CursorRow();
    int ccol = buffer_.CursorCol();
    if (crow >= 0 && crow < rows && ccol >= 0 && ccol < cols) {
      float cx = origin_x + ccol * cell_width_;
      float cy = origin_y + crow * cell_height_;
      SkPaint cursor_paint;
      cursor_paint.setAntiAlias(false);
      cursor_paint.setColor(buffer_.GetCursorColor());

      bool is_focused =
          window_focused_ && (!focusable_ || focusable_->HasFocus());
      if (!is_focused) {
        cursor_paint.setStyle(SkPaint::kStroke_Style);
        cursor_paint.setStrokeWidth(kUnfocusedCursorStrokeWidth);
        canvas.drawRect(
            SkRect::MakeXYWH(cx + 0.5f, cy + 0.5f, cell_width_ - 1.0f,
                             cell_height_ - 1.0f),
            cursor_paint);
      } else if (!buffer_.IsCursorBlinking() || cursor_blink_phase_on_) {
        int style = buffer_.CursorStyle();
        cursor_paint.setStyle(SkPaint::kFill_Style);
        if (style == 3 || style == 4) {
          canvas.drawRect(
              SkRect::MakeXYWH(cx, cy + cell_height_ - kCursorThickness,
                               cell_width_, kCursorThickness),
              cursor_paint);
        } else if (style == 5 || style == 6) {
          canvas.drawRect(
              SkRect::MakeXYWH(cx, cy, kCursorThickness, cell_height_),
              cursor_paint);
        } else {
          canvas.drawRect(SkRect::MakeXYWH(cx, cy, cell_width_, cell_height_),
                          cursor_paint);
          const Line& line = buffer_.GetVisibleLine(crow);
          if (ccol < static_cast<int>(line.cells.size())) {
            const Cell& cell = line.cells[ccol];
            if (cell.codepoint > U' ' && cell.cell_width > 0) {
              SkFont* font = GetMonaspaceUiFont(
                  static_cast<MonaspaceFamily>(
                      std::min<uint8>(cell.font_family, 4)),
                  kDefaultFontSize, (cell.flags & kCellFlagBold) != 0,
                  (cell.flags & kCellFlagItalic) != 0);
              if (font) {
                char utf8[4];
                size_t utf8_len = EncodeUtf8Codepoint(cell.codepoint, utf8);
                fg_paint.setColor(buffer_.GetDefaultBackgroundColor());
                canvas.drawSimpleText(utf8, utf8_len, SkTextEncoding::kUTF8, cx,
                                      cy + cell_baseline_, *font, fg_paint);
              }
            }
          }
        }
      }
    }
  }

  canvas.restore();
}

Size TerminalWidget::Measure(float width, YGMeasureMode width_mode,
                             float height, YGMeasureMode height_mode) {
  UpdateFontMetrics();
  float pref_w = kDefaultTerminalCols * cell_width_;
  float pref_h = kDefaultTerminalRows * cell_height_;

  float out_w = pref_w;
  if (width_mode == YGMeasureModeExactly)
    out_w = width;
  else if (width_mode == YGMeasureModeAtMost)
    out_w = std::min(pref_w, width);

  float out_h = pref_h;
  if (height_mode == YGMeasureModeExactly)
    out_h = height;
  else if (height_mode == YGMeasureModeAtMost)
    out_h = std::min(pref_h, height);

  return {.width = out_w, .height = out_h};
}

bool TerminalWidget::DrawCustomGlyph(SkCanvas& canvas, uint32_t cp, float x,
                                     float y, float w, float h,
                                     uint32_t fg_color) {
  if (cp >= 0x2580 && cp <= 0x259F) {
    SkPaint paint;
    paint.setStyle(SkPaint::kFill_Style);
    paint.setAntiAlias(false);
    paint.setColor(fg_color);

    if (cp == 0x2580) {
      canvas.drawRect(SkRect::MakeXYWH(x, y, w, h * 0.5f), paint);
      return true;
    }
    if (cp >= 0x2581 && cp <= 0x2588) {
      float frac = static_cast<float>(cp - 0x2580) / 8.0f;
      float bh = h * frac;
      canvas.drawRect(SkRect::MakeXYWH(x, y + h - bh, w, bh), paint);
      return true;
    }
    if (cp >= 0x2589 && cp <= 0x258F) {
      float frac = static_cast<float>(8 - (cp - 0x2588)) / 8.0f;
      canvas.drawRect(SkRect::MakeXYWH(x, y, w * frac, h), paint);
      return true;
    }
    if (cp == 0x2590) {
      canvas.drawRect(SkRect::MakeXYWH(x + w * 0.5f, y, w * 0.5f, h), paint);
      return true;
    }
    if (cp >= 0x2591 && cp <= 0x2593) {
      uint32 alpha = (cp == 0x2591) ? 64 : ((cp == 0x2592) ? 128 : 192);
      paint.setColor((fg_color & 0x00FFFFFFu) | (alpha << 24));
      canvas.drawRect(SkRect::MakeXYWH(x, y, w, h), paint);
      return true;
    }
    if (cp == 0x2594) {
      canvas.drawRect(SkRect::MakeXYWH(x, y, w, h / 8.0f), paint);
      return true;
    }
    if (cp == 0x2595) {
      canvas.drawRect(SkRect::MakeXYWH(x + w * (7.0f / 8.0f), y, w / 8.0f, h),
                      paint);
      return true;
    }
    // Quadrant blocks U+2596 .. U+259F (bit0=UL, bit1=UR, bit2=LL, bit3=LR).
    static constexpr uint8 kQuadrantMasks[10] = {
        0b0100, 0b1000, 0b0001, 0b1101, 0b1001,
        0b0111, 0b1011, 0b0010, 0b0110, 0b1110};
    uint8 mask = kQuadrantMasks[cp - 0x2596];
    float hw = w * 0.5f;
    float hh = h * 0.5f;
    if (mask & 1)
      canvas.drawRect(SkRect::MakeXYWH(x, y, hw, hh), paint);
    if (mask & 2)
      canvas.drawRect(SkRect::MakeXYWH(x + hw, y, w - hw, hh), paint);
    if (mask & 4)
      canvas.drawRect(SkRect::MakeXYWH(x, y + hh, hw, h - hh), paint);
    if (mask & 8)
      canvas.drawRect(SkRect::MakeXYWH(x + hw, y + hh, w - hw, h - hh), paint);
    return true;
  }

  if (cp >= 0x2800 && cp <= 0x28FF) {
    uint8 bits = static_cast<uint8>(cp - 0x2800);
    if (bits == 0)
      return true;
    SkPaint paint;
    paint.setStyle(SkPaint::kFill_Style);
    paint.setAntiAlias(true);
    paint.setColor(fg_color);

    float dot_w = std::max(1.5f, std::floor(w * 0.28f));
    float dot_h = std::max(1.5f, std::floor(h * 0.16f));
    float col_x[2] = {x + w * 0.25f - dot_w * 0.5f,
                      x + w * 0.75f - dot_w * 0.5f};
    float row_y[4] = {
        y + h * 0.125f - dot_h * 0.5f, y + h * 0.375f - dot_h * 0.5f,
        y + h * 0.625f - dot_h * 0.5f, y + h * 0.875f - dot_h * 0.5f};

    static constexpr int kBitCol[8] = {0, 0, 0, 1, 1, 1, 0, 1};
    static constexpr int kBitRow[8] = {0, 1, 2, 0, 1, 2, 3, 3};
    for (int b = 0; b < 8; ++b) {
      if (bits & (1 << b))
        canvas.drawRect(
            SkRect::MakeXYWH(col_x[kBitCol[b]], row_y[kBitRow[b]], dot_w,
                             dot_h),
            paint);
    }
    return true;
  }

  if (cp >= 0x2500 && cp <= 0x257F) {
    float cx = std::floor(x + w * 0.5f) + 0.5f;
    float cy = std::floor(y + h * 0.5f) + 0.5f;
    SkPaint paint;
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setAntiAlias(false);
    paint.setColor(fg_color);

    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;
    bool heavy = false;

    switch (cp) {
      case 0x2500:
      case 0x2501:
      case 0x2550:
        left = right = true;
        heavy = (cp == 0x2501);
        break;
      case 0x2502:
      case 0x2503:
      case 0x2551:
        up = down = true;
        heavy = (cp == 0x2503);
        break;
      case 0x250C:
      case 0x250F:
      case 0x2554:
      case 0x256D:
        right = down = true;
        heavy = (cp == 0x250F);
        break;
      case 0x2510:
      case 0x2513:
      case 0x2557:
      case 0x256E:
        left = down = true;
        heavy = (cp == 0x2513);
        break;
      case 0x2514:
      case 0x2517:
      case 0x255A:
      case 0x2570:
        right = up = true;
        heavy = (cp == 0x2517);
        break;
      case 0x2518:
      case 0x251B:
      case 0x255D:
      case 0x256F:
        left = up = true;
        heavy = (cp == 0x251B);
        break;
      case 0x251C:
      case 0x2523:
      case 0x2560:
        up = down = right = true;
        heavy = (cp == 0x2523);
        break;
      case 0x2524:
      case 0x252B:
      case 0x2563:
        up = down = left = true;
        heavy = (cp == 0x252B);
        break;
      case 0x252C:
      case 0x2533:
      case 0x2566:
        left = right = down = true;
        heavy = (cp == 0x2533);
        break;
      case 0x2534:
      case 0x253B:
      case 0x2569:
        left = right = up = true;
        heavy = (cp == 0x253B);
        break;
      case 0x253C:
      case 0x254B:
      case 0x256C:
        left = right = up = down = true;
        heavy = (cp == 0x254B);
        break;
      case 0x2574:
      case 0x2578:
        left = true;
        heavy = (cp == 0x2578);
        break;
      case 0x2575:
      case 0x2579:
        up = true;
        heavy = (cp == 0x2579);
        break;
      case 0x2576:
      case 0x257A:
        right = true;
        heavy = (cp == 0x257A);
        break;
      case 0x2577:
      case 0x257B:
        down = true;
        heavy = (cp == 0x257B);
        break;
      default:
        return false;
    }

    paint.setStrokeWidth(heavy ? 2.0f : 1.0f);
    if (left)
      canvas.drawLine(x, cy, cx, cy, paint);
    if (right)
      canvas.drawLine(cx, cy, x + w, cy, paint);
    if (up)
      canvas.drawLine(cx, y, cx, cy, paint);
    if (down)
      canvas.drawLine(cx, cy, cx, y + h, paint);
    return true;
  }

  return false;
}

void TerminalWidget::PointToCell(const Point& point, int& row, int& col) const {
  int r = (cell_height_ > 0.0f)
              ? static_cast<int>(std::floor(point.y / cell_height_))
              : 0;
  int c = (cell_width_ > 0.0f)
              ? static_cast<int>(std::floor(point.x / cell_width_))
              : 0;
  row = std::clamp(r, 0, std::max(0, buffer_.Rows() - 1));
  col = std::clamp(c, 0, std::max(0, buffer_.Cols() - 1));
}

size_t TerminalWidget::VisibleRowToAbsoluteLine(int visible_row) const {
  int top_abs = buffer_.ScrollbackSize() - buffer_.ScrollOffsetLines();
  return static_cast<size_t>(std::max(0, top_abs + visible_row));
}

bool TerminalWidget::GetNormalizedSelection(size_t& start_line, int& start_col,
                                            size_t& end_line,
                                            int& end_col) const {
  if (!has_selection_)
    return false;
  if (selection_anchor_line_ < selection_end_line_ ||
      (selection_anchor_line_ == selection_end_line_ &&
       selection_anchor_col_ <= selection_end_col_)) {
    start_line = selection_anchor_line_;
    start_col = selection_anchor_col_;
    end_line = selection_end_line_;
    end_col = selection_end_col_;
  } else {
    start_line = selection_end_line_;
    start_col = selection_end_col_;
    end_line = selection_anchor_line_;
    end_col = selection_anchor_col_;
  }
  return !(start_line == end_line && start_col == end_col);
}

void TerminalWidget::ClearSelection() {
  if (!has_selection_ && !is_selecting_)
    return;
  has_selection_ = false;
  is_selecting_ = false;
  buffer_.MarkAllDirty();
  InvalidateDirtyRows();
}

void TerminalWidget::UpdateCursorAndHoverAt(const Point& point) {
  auto strong_node = node_.lock();
  if (!strong_node)
    return;

  int row = 0;
  int col = 0;
  PointToCell(point, row, col);

  uint16 link_id = 0;
  const Line& line = buffer_.GetVisibleLine(row);
  if (col >= 0 && col < static_cast<int>(line.cells.size()))
    link_id = line.cells[col].hyperlink_id;

  if (link_id != hovered_hyperlink_id_) {
    hovered_hyperlink_id_ = link_id;
    buffer_.MarkAllDirty();
    InvalidateDirtyRows();
  }

  Cursor desired_cursor = Cursor::Caret;
  if (link_id != 0) {
    desired_cursor = Cursor::Poke;
    std::string_view url = buffer_.GetHyperlinkUrl(link_id);
    if (tooltip_) {
      tooltip_->SetText(url);
      tooltip_->ShowTooltipAt(point);
    }
  } else {
    if (tooltip_) {
      tooltip_->SetText("");
      tooltip_->HideTooltip();
    }
    if (kitty_input_.HasPointerShapeOverride())
      desired_cursor = kitty_input_.GetPointerShape();
  }

  strong_node->SetCursor(desired_cursor);
  if (desired_cursor != last_applied_cursor_) {
    last_applied_cursor_ = desired_cursor;
    if (on_cursor_changed_callback_)
      on_cursor_changed_callback_(desired_cursor);
  }
}

void TerminalWidget::StartBlinkTimer() {
  if (blink_timer_running_)
    return;
  blink_timer_running_ = true;

  auto schedule_next = std::make_shared<std::function<void()>>();
  *schedule_next = [this, alive = alive_token_, schedule_next]() {
    ::perception::AfterDuration(
        std::chrono::milliseconds(kCursorBlinkIntervalMs),
        [this, alive, schedule_next]() {
          if (!*alive)
            return;
          cursor_blink_phase_on_ = !cursor_blink_phase_on_;

          bool is_focused =
              window_focused_ && (!focusable_ || focusable_->HasFocus());
          if (is_focused && buffer_.IsCursorVisible() &&
              buffer_.IsCursorBlinking() && buffer_.ScrollOffsetLines() == 0) {
            buffer_.MarkRowDirty(buffer_.CursorRow());
          }

          int rows = buffer_.Rows();
          for (int r = 0; r < rows; ++r) {
            const Line& line = buffer_.GetVisibleLine(r);
            for (const Cell& cell : line.cells) {
              if (cell.flags & kCellFlagBlink) {
                buffer_.MarkRowDirty(r);
                break;
              }
            }
          }

          if (!buffer_.IsSynchronizedOutput())
            InvalidateDirtyRows();

          (*schedule_next)();
        });
  };

  (*schedule_next)();
}

void TerminalWidget::ResetCursorBlinkPhase() {
  if (!cursor_blink_phase_on_) {
    cursor_blink_phase_on_ = true;
    buffer_.MarkRowDirty(buffer_.CursorRow());
  }
}

void TerminalWidget::ShowContextMenu(const Point& point) {
  auto strong_node = node_.lock();
  if (!strong_node)
    return;

  Point abs_anchor = strong_node->GetAbsolutePosition() + point;
  auto menu = PopUpMenu::Container(
      PopUpMenu::ItemWithIconAndShortcut("Copy", nullptr, "Ctrl+Shift+C",
                                         [this]() { CopySelection(); }),
      PopUpMenu::ItemWithIconAndShortcut("Paste", nullptr, "Ctrl+Shift+V",
                                         [this]() { PasteClipboard(); }),
      PopUpMenu::Divider(),
      PopUpMenu::ItemWithIconAndShortcut("Select All", nullptr, "Ctrl+Shift+A",
                                         [this]() { SelectAll(); }),
      PopUpMenu::ItemWithIconAndShortcut("Clear Scrollback", nullptr, "",
                                         [this]() { ClearScrollback(); }));
  PopUp::Show(strong_node, abs_anchor, menu);
}

void TerminalWidget::HandleKeyDown(const KeyboardKeyEvent& event) {
  uint8 scancode = event.key;
  KeyCode key = static_cast<KeyCode>(scancode);

  if (IsShiftKey(scancode)) {
    shift_pressed_ = true;
  } else if (IsControlKey(scancode)) {
    ctrl_pressed_ = true;
  } else if (key == KeyCode::LeftAlt) {
    alt_pressed_ = true;
  } else if (key == KeyCode::LeftCommand || key == KeyCode::RightCommand) {
    super_pressed_ = true;
  }

  // Handle local terminal shortcuts (Ctrl+Shift+C/V/A, Shift+PageUp/PageDown).
  if (ctrl_pressed_ && shift_pressed_) {
    char ascii = ScancodeToAscii(scancode, false);
    if (ascii == 'c') {
      CopySelection();
      return;
    }
    if (ascii == 'v') {
      PasteClipboard();
      return;
    }
    if (ascii == 'a') {
      SelectAll();
      return;
    }
  }

  if (shift_pressed_ && !buffer_.IsAlternateScreen()) {
    int page_step = std::max(1, buffer_.Rows() - 1);
    if (key == KeyCode::PageUp) {
      buffer_.SetScrollOffsetLines(buffer_.ScrollOffsetLines() + page_step);
      SyncScrollBar();
      InvalidateDirtyRows();
      return;
    }
    if (key == KeyCode::PageDown) {
      buffer_.SetScrollOffsetLines(buffer_.ScrollOffsetLines() - page_step);
      SyncScrollBar();
      InvalidateDirtyRows();
      return;
    }
  }

  // Modifier-only key presses are only reported when Kitty ReportAllAsEscapeCodes is set.
  bool is_modifier_only =
      IsShiftKey(scancode) || IsControlKey(scancode) ||
      key == KeyCode::LeftAlt || key == KeyCode::LeftCommand ||
      key == KeyCode::RightCommand;
  uint32 kitty_flags =
      kitty_input_.GetKeyboardFlags(buffer_.IsAlternateScreen());
  if (is_modifier_only && !(kitty_flags & kKittyKeyReportAllAsEscapeCodes))
    return;

  if (buffer_.ScrollOffsetLines() > 0) {
    buffer_.SetScrollOffsetLines(0);
    SyncScrollBar();
  }
  ClearSelection();
  ResetCursorBlinkPhase();

  std::string encoded = kitty_input_.EncodeKeyEvent(
      scancode, shift_pressed_, alt_pressed_, ctrl_pressed_, super_pressed_, 1,
      buffer_.IsAlternateScreen(), buffer_.IsApplicationCursorKeys());
  if (encoded.empty())
    return;

  if (!encoded.empty() && encoded[0] == '\x1b')
    SendToPty(encoded);
  else
    ProcessUserInputBytes(encoded);
}

void TerminalWidget::HandleKeyUp(const KeyboardKeyEvent& event) {
  uint8 scancode = event.key;
  KeyCode key = static_cast<KeyCode>(scancode);

  if (IsShiftKey(scancode))
    shift_pressed_ = false;
  else if (IsControlKey(scancode))
    ctrl_pressed_ = false;
  else if (key == KeyCode::LeftAlt)
    alt_pressed_ = false;
  else if (key == KeyCode::LeftCommand || key == KeyCode::RightCommand)
    super_pressed_ = false;

  uint32 kitty_flags =
      kitty_input_.GetKeyboardFlags(buffer_.IsAlternateScreen());
  if (kitty_flags & kKittyKeyReportEventTypes) {
    std::string encoded = kitty_input_.EncodeKeyEvent(
        scancode, shift_pressed_, alt_pressed_, ctrl_pressed_, super_pressed_,
        3, buffer_.IsAlternateScreen(), buffer_.IsApplicationCursorKeys());
    if (!encoded.empty())
      SendToPty(encoded);
  }
}

void TerminalWidget::HandleMouseHover(const Point& point) {
  last_mouse_point_ = point;
  UpdateCursorAndHoverAt(point);

  int row = 0;
  int col = 0;
  PointToCell(point, row, col);

  int tracking_mode = buffer_.MouseTrackingMode();
  if (tracking_mode != 0 && !shift_pressed_) {
    bool should_report =
        (tracking_mode == 1003) ||
        (tracking_mode == 1002 && active_mouse_button_ != MouseButton::Unknown);
    if (should_report) {
      std::string seq = kitty_input_.EncodeMouseEvent(
          active_mouse_button_, false, true, 0, col, row,
          static_cast<int>(point.x), static_cast<int>(point.y), shift_pressed_,
          alt_pressed_, ctrl_pressed_, buffer_.MouseEncodingMode());
      SendToPty(seq);
    }
    return;
  }

  if (is_selecting_) {
    selection_end_line_ = VisibleRowToAbsoluteLine(row);
    selection_end_col_ = col;
    has_selection_ = true;
    buffer_.MarkAllDirty();
    InvalidateDirtyRows();
  }
}

void TerminalWidget::HandleMouseLeave() {
  if (hovered_hyperlink_id_ != 0) {
    hovered_hyperlink_id_ = 0;
    buffer_.MarkAllDirty();
    InvalidateDirtyRows();
  }
  if (tooltip_)
    tooltip_->HideTooltip();
}

void TerminalWidget::HandleMouseButtonDown(const Point& point,
                                           MouseButton button) {
  last_mouse_point_ = point;
  if (focusable_)
    focusable_->Focus();

  int row = 0;
  int col = 0;
  PointToCell(point, row, col);

  int tracking_mode = buffer_.MouseTrackingMode();
  if (tracking_mode != 0 && !shift_pressed_) {
    active_mouse_button_ = button;
    std::string seq = kitty_input_.EncodeMouseEvent(
        button, false, false, 0, col, row, static_cast<int>(point.x),
        static_cast<int>(point.y), shift_pressed_, alt_pressed_, ctrl_pressed_,
        buffer_.MouseEncodingMode());
    SendToPty(seq);
    return;
  }

  if (button == MouseButton::Right) {
    ShowContextMenu(point);
    return;
  }

  if (button == MouseButton::Middle) {
    PasteClipboard();
    return;
  }

  if (button == MouseButton::Left) {
    if (ctrl_pressed_ && hovered_hyperlink_id_ != 0) {
      std::string_view url = buffer_.GetHyperlinkUrl(hovered_hyperlink_id_);
      if (!url.empty())
        ::perception::SetClipboard(url);
      return;
    }

    size_t abs_line = VisibleRowToAbsoluteLine(row);
    if (shift_pressed_ && has_selection_) {
      selection_end_line_ = abs_line;
      selection_end_col_ = col;
    } else {
      selection_anchor_line_ = abs_line;
      selection_anchor_col_ = col;
      selection_end_line_ = abs_line;
      selection_end_col_ = col;
      has_selection_ = false;
    }
    is_selecting_ = true;
    buffer_.MarkAllDirty();
    InvalidateDirtyRows();
  }
}

void TerminalWidget::HandleMouseButtonUp(const Point& point,
                                         MouseButton button) {
  last_mouse_point_ = point;
  int row = 0;
  int col = 0;
  PointToCell(point, row, col);

  int tracking_mode = buffer_.MouseTrackingMode();
  if (tracking_mode != 0 && !shift_pressed_) {
    active_mouse_button_ = MouseButton::Unknown;
    std::string seq = kitty_input_.EncodeMouseEvent(
        button, true, false, 0, col, row, static_cast<int>(point.x),
        static_cast<int>(point.y), shift_pressed_, alt_pressed_, ctrl_pressed_,
        buffer_.MouseEncodingMode());
    SendToPty(seq);
    return;
  }

  if (button == MouseButton::Left)
    is_selecting_ = false;
}

Point TerminalWidget::HandleMouseScroll(const Point& point,
                                        const Point& delta) {
  last_mouse_point_ = point;
  if (delta.y == 0.0f)
    return Point{.x = 0.0f, .y = 0.0f};

  int row = 0;
  int col = 0;
  PointToCell(point, row, col);

  int tracking_mode = buffer_.MouseTrackingMode();
  if (tracking_mode != 0 && !shift_pressed_) {
    int wheel_delta = (delta.y < 0.0f) ? 1 : -1;
    std::string seq = kitty_input_.EncodeMouseEvent(
        MouseButton::Unknown, false, false, wheel_delta, col, row,
        static_cast<int>(point.x), static_cast<int>(point.y), shift_pressed_,
        alt_pressed_, ctrl_pressed_, buffer_.MouseEncodingMode());
    SendToPty(seq);
    return Point{.x = 0.0f, .y = delta.y};
  }

  if (buffer_.IsAlternateScreen())
    return Point{.x = 0.0f, .y = 0.0f};

  float row_height =
      (cell_height_ > 0.0f) ? cell_height_ : kFallbackCellHeight;
  int line_delta = static_cast<int>(std::round(delta.y / row_height));
  if (line_delta == 0)
    line_delta = (delta.y > 0.0f) ? 1 : -1;

  int old_offset = buffer_.ScrollOffsetLines();
  buffer_.SetScrollOffsetLines(old_offset - line_delta);
  int new_offset = buffer_.ScrollOffsetLines();
  if (new_offset == old_offset)
    return Point{.x = 0.0f, .y = 0.0f};

  SyncScrollBar();
  InvalidateDirtyRows();
  float consumed_y = static_cast<float>(old_offset - new_offset) * row_height;
  return Point{.x = 0.0f, .y = consumed_y};
}

