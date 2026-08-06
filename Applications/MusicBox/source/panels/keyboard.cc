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

#include "panels/keyboard.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "constants.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "perception/ui/components/focusable.h"
#include "perception/ui/components/tooltip.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"

using ::perception::ui::DrawContext;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::Size;
using ::perception::ui::components::Focusable;
using ::perception::ui::components::Tooltip;
namespace panels {

namespace window = ::perception::window;

namespace {

// Keyboard constants.

// Default total height in pixels for the keyboard panel node.
constexpr float kKeyboardHeight = 135.0f;
// Black key width relative to white key width.
constexpr float kBlackKeyWidthScale = 0.65f;
// Black key height relative to total keyboard height.
constexpr float kBlackKeyHeightScale = 0.62f;
// White key index offset for C1 (A0 is index 0, B0 is index 1, C1 is index 2).
constexpr float kOctave1CWhiteKeyOffset = 2.0f;
// Minimum valid octave index on a standard piano keyboard.
constexpr int kMinOctave = 1;
// Maximum valid octave index on a standard piano keyboard.
constexpr int kMaxOctave = 7;

// Layout and font constants.

// Font size in pt for key binding labels.
constexpr float kKeyOverlayFontSize = 10.0f;
// Vertical padding of label from bottom of black key.
constexpr float kBlackKeyLabelBottomPadding = 6.0f;
// Vertical padding of label from bottom of white key.
constexpr float kWhiteKeyLabelBottomPadding = 8.0f;

// Color constants.

// Default background for white keys (white).
constexpr SkColor kWhiteKeyBgColor = SkColorSetARGB(0xFF, 0xFF, 0xFF, 0xFF);
// Default pressed color for white keys (light blue).
constexpr SkColor kWhiteKeyPressedColor =
    SkColorSetARGB(0xFF, 0x93, 0xC5, 0xFD);
// Border color for white keys (gray).
constexpr SkColor kWhiteKeyStrokeColor = SkColorSetARGB(0xFF, 0x9C, 0xA3, 0xAF);
// Default background for black keys (dark gray).
constexpr SkColor kBlackKeyBgColor = SkColorSetARGB(0xFF, 0x11, 0x18, 0x27);
// Default pressed color for black keys (blue).
constexpr SkColor kBlackKeyPressedColor =
    SkColorSetARGB(0xFF, 0x25, 0x63, 0xEB);
// Border color for black keys (black).
constexpr SkColor kBlackKeyStrokeColor = SkColorSetARGB(0xFF, 0x00, 0x00, 0x00);
// Text label color on unpressed white key.
constexpr SkColor kWhiteKeyLabelColor = SkColorSetARGB(0xFF, 0x4B, 0x55, 0x63);
// Text label color on pressed white key.
constexpr SkColor kWhiteKeyPressedLabelColor =
    SkColorSetARGB(0xFF, 0x11, 0x18, 0x27);
// Text label color on black key (light gray).
constexpr SkColor kBlackKeyLabelColor = SkColorSetARGB(0xFF, 0xF3, 0xF4, 0xF6);

// Layout and color constants for octave indicator bands.

// Height of the octave indicator region underneath the piano keys.
constexpr float kOctaveBandsTotalHeight = 28.0f;
// Height of each octave indicator band row.
constexpr float kOctaveBandRowHeight = 11.0f;
// Gap between the lower and upper octave indicator band rows.
constexpr float kOctaveBandRowGap = 2.0f;

// Background color for the octave bands track area (dark gray).
constexpr SkColor kOctaveTrackBgColor = SkColorSetARGB(0xFF, 0x11, 0x18, 0x27);
// Fill color for the lower octave band (blue).
constexpr SkColor kLowerBandFillColor = SkColorSetARGB(0xE0, 0x25, 0x63, 0xEB);
// Border color for the lower octave band (light blue).
constexpr SkColor kLowerBandStrokeColor =
    SkColorSetARGB(0xFF, 0x60, 0xA5, 0xFA);
// Fill color for the upper octave band (emerald).
constexpr SkColor kUpperBandFillColor = SkColorSetARGB(0xE0, 0x05, 0x96, 0x69);
// Border color for the upper octave band (light emerald).
constexpr SkColor kUpperBandStrokeColor =
    SkColorSetARGB(0xFF, 0x34, 0xD3, 0x99);
// Label text color for octave indicator bands (white).
constexpr SkColor kOctaveBandTextColor = SkColorSetARGB(0xFF, 0xFF, 0xFF, 0xFF);
// Font size for octave indicator band labels.
constexpr float kOctaveBandFontSize = 9.0f;

// Map indicating whether each semitone index in an octave is a black key (true)
// or white key (false).
constexpr bool kIsBlackKey[kSemitonesPerOctave] = {false, true,  false, false,
                                                   true,  false, true,  false,
                                                   false, true,  false, true};

// Cumulative count of white keys for each semitone index in an octave.
constexpr int kWhiteKeyIndexInOctave[kSemitonesPerOctave] = {0, 1, 1, 2, 3, 3,
                                                             4, 4, 5, 6, 6, 7};

std::string_view GetKeyLabel(int k, int bottom_octave, int top_octave) {
  int b_start = (bottom_octave - 1) * kSemitonesPerOctave + kKeyOffsetC;
  int t_start = (top_octave - 1) * kSemitonesPerOctave + kKeyOffsetC;

  if (k >= b_start && k < b_start + kSemitonesPerOctave) {
    int semitone = k - b_start;
    static constexpr std::string_view kBottomLabels[kSemitonesPerOctave] = {
        "Z", "S", "X", "D", "C", "V", "G", "B", "H", "N", "J", "M"};
    return kBottomLabels[semitone];
  }

  if (k >= t_start && k < t_start + kSemitonesPerOctave) {
    int semitone = k - t_start;
    static constexpr std::string_view kTopLabels[kSemitonesPerOctave] = {
        "Q", "2", "W", "3", "E", "R", "5", "T", "6", "Y", "7", "U"};
    return kTopLabels[semitone];
  }
  return {};
}

// Calculates the octave index (1 to 7) at the specified X coordinate.
int CalculateOctaveFromX(float x, float width) {
  float white_key_w = width / kTotalWhiteKeys;
  float white_idx = x / white_key_w;
  int octave = static_cast<int>(
                   std::floor((white_idx - kOctave1CWhiteKeyOffset) /
                              static_cast<float>(kDiatonicStepsPerOctave))) +
               1;
  return std::clamp(octave, kMinOctave, kMaxOctave);
}

}  // namespace

Keyboard::Keyboard(TrackManager& track_manager)
    : track_manager_(track_manager) {
  BuildNode();
}

void Keyboard::SetOctaves(int bottom_octave, int top_octave) {
  int clamped_bottom = std::clamp(bottom_octave, kMinOctave, kMaxOctave);
  int clamped_top = std::clamp(top_octave, kMinOctave, kMaxOctave);
  if (bottom_octave_ != clamped_bottom || top_octave_ != clamped_top) {
    bottom_octave_ = clamped_bottom;
    top_octave_ = clamped_top;
    Invalidate();
  }
}

void Keyboard::OnOctavesChanged(
    std::function<void(int bottom_octave, int top_octave)> callback) {
  on_octaves_changed_ = std::move(callback);
}

void Keyboard::Invalidate() {
  if (node_) {
    node_->Invalidate();
  }
}

void Keyboard::OnKeyHover(
    std::function<void(int key_index, const Point& pt)> callback) {
  on_key_hover_ = std::move(callback);
}

void Keyboard::OnKeyDown(std::function<void(int key_index)> callback) {
  on_key_down_ = std::move(callback);
}

void Keyboard::OnKeyUp(std::function<void(int key_index)> callback) {
  on_key_up_ = std::move(callback);
}

void Keyboard::SetKeyPressed(int key_index, bool is_pressed) {
  if (is_pressed) {
    if (pressed_keys_.insert(key_index).second) {
      Invalidate();
    }
  } else {
    if (pressed_keys_.erase(key_index) > 0) {
      Invalidate();
    }
  }
}

bool Keyboard::IsKeyPressed(int key_index) const {
  return pressed_keys_.count(key_index) > 0;
}

bool Keyboard::IsKeyBlack(int key_index) {
  if (key_index < 0 || key_index >= kTotalPianoKeys) return false;
  return kIsBlackKey[key_index % kSemitonesPerOctave];
}

int Keyboard::GetWhiteKeyCount(int key_index) {
  if (key_index < 0 || key_index >= kTotalPianoKeys) return 0;
  int octave = key_index / kSemitonesPerOctave;
  int note = key_index % kSemitonesPerOctave;
  return octave * 7 + kWhiteKeyIndexInOctave[note];
}

int Keyboard::GetKeyAtPosition(const Point& point, const Size& size) {
  if (size.width <= 0 || size.height <= 0) return -1;
  float piano_h = size.height - kOctaveBandsTotalHeight;
  if (point.y > piano_h) return -1;

  float white_key_w = size.width / kTotalWhiteKeys;
  float black_key_w = white_key_w * kBlackKeyWidthScale;
  float black_key_h = piano_h * kBlackKeyHeightScale;

  // Check black keys first (top layer)
  if (point.y <= black_key_h) {
    for (int k = 0; k < kTotalPianoKeys; ++k) {
      if (!IsKeyBlack(k)) continue;
      int w_idx = GetWhiteKeyCount(k);
      float x_center = w_idx * white_key_w;
      float x_left = x_center - (black_key_w / 2.0f);
      if (point.x >= x_left && point.x <= x_left + black_key_w) {
        return k;
      }
    }
  }

  // Check white keys
  int w_idx = static_cast<int>(point.x / white_key_w);
  w_idx = std::clamp(w_idx, 0, static_cast<int>(kTotalWhiteKeys) - 1);

  for (int k = 0; k < kTotalPianoKeys; ++k) {
    if (!IsKeyBlack(k) && GetWhiteKeyCount(k) == w_idx) {
      return k;
    }
  }

  return -1;
}

void Keyboard::BuildNode() {
  node_ = Node::Empty(
      [](Layout& layout) {
        layout.SetAlignSelf(YGAlignStretch);
        layout.SetHeight(kKeyboardHeight);
      },
      [this](Node& node) {
        node.OnDraw([this, &node](const DrawContext& context) {
          if (!context.skia_canvas) return;
          SkCanvas* canvas = context.skia_canvas;
          canvas->save();
          canvas->translate(context.area.origin.x, context.area.origin.y);
          Size size{context.area.Width(), context.area.Height()};
          Draw(*canvas, size);
          canvas->restore();
        });

        auto tooltip = node.GetOrAdd<Tooltip>();
        auto focusable = node.GetOrAdd<Focusable>();

        node.OnMouseHover([this, &node, focusable, tooltip](const Point& pt) {
          if (!focusable->HasFocus()) {
            focusable->Focus();
          }
          Size s = node.GetSize();
          float piano_h = s.height - kOctaveBandsTotalHeight;

          if (dragging_bottom_octave_) {
            node.SetCursor(window::Cursor::Grab);
            int target = CalculateOctaveFromX(pt.x, s.width);
            if (target != bottom_octave_) {
              SetOctaves(target, top_octave_);
              if (on_octaves_changed_) {
                on_octaves_changed_(bottom_octave_, top_octave_);
              }
            }
            tooltip->HideTooltip();
            return;
          }

          if (dragging_top_octave_) {
            node.SetCursor(window::Cursor::Grab);
            int target = CalculateOctaveFromX(pt.x, s.width);
            if (target != top_octave_) {
              SetOctaves(bottom_octave_, target);
              if (on_octaves_changed_) {
                on_octaves_changed_(bottom_octave_, top_octave_);
              }
            }
            tooltip->HideTooltip();
            return;
          }

          if (pt.y > piano_h) {
            node.SetCursor(window::Cursor::Grab);
            tooltip->HideTooltip();
            return;
          }

          node.SetCursor(window::Cursor::Pointer);

          int hit_key = GetKeyAtPosition(pt, s);
          if (hit_key >= 0 && hit_key < kTotalPianoKeys) {
            std::string note_name = TrackManager::KeyIndexToNoteName(hit_key);
            tooltip->SetText(note_name);
            tooltip->ShowTooltipAt(pt);
          } else {
            tooltip->HideTooltip();
          }
          if (on_key_hover_) {
            on_key_hover_(hit_key, pt);
          }
        });

        node.OnMouseLeave([this, &node, tooltip]() {
          dragging_bottom_octave_ = false;
          dragging_top_octave_ = false;
          node.SetCursor(window::Cursor::Pointer);
          tooltip->HideTooltip();
        });

        node.OnMouseButtonDown(
            [this, &node](const Point& pt, window::MouseButton button) {
              Size s = node.GetSize();
              float piano_h = s.height - kOctaveBandsTotalHeight;
              if (pt.y > piano_h) {
                int target = CalculateOctaveFromX(pt.x, s.width);
                float row1_y = piano_h + 2.0f + kOctaveBandRowHeight;
                if (pt.y <= row1_y) {
                  dragging_top_octave_ = true;
                  SetOctaves(bottom_octave_, target);
                } else {
                  dragging_bottom_octave_ = true;
                  SetOctaves(target, top_octave_);
                }
                if (on_octaves_changed_) {
                  on_octaves_changed_(bottom_octave_, top_octave_);
                }
                return;
              }

              int hit_key = GetKeyAtPosition(pt, s);
              if (hit_key >= 0 && hit_key < kTotalPianoKeys) {
                if (on_key_down_) {
                  on_key_down_(hit_key);
                }
              }
            });

        node.OnMouseButtonUp(
            [this, &node](const Point& pt, window::MouseButton button) {
              if (dragging_bottom_octave_ || dragging_top_octave_) {
                dragging_bottom_octave_ = false;
                dragging_top_octave_ = false;
                return;
              }
              Size s = node.GetSize();
              int hit_key = GetKeyAtPosition(pt, s);
              if (hit_key >= 0 && hit_key < kTotalPianoKeys) {
                if (on_key_up_) {
                  on_key_up_(hit_key);
                }
              }
            });
      });
}

void Keyboard::Draw(SkCanvas& canvas, const Size& size) {
  float white_key_w = size.width / kTotalWhiteKeys;
  float black_key_w = white_key_w * kBlackKeyWidthScale;
  float piano_h = size.height - kOctaveBandsTotalHeight;
  float black_key_h = piano_h * kBlackKeyHeightScale;

  auto active_highlights = track_manager_.GetActiveKeyHighlights();
  static SkFont* font = perception::ui::GetUiFont("", kKeyOverlayFontSize);
  static SkFont* band_font = perception::ui::GetUiFont("", kOctaveBandFontSize);

  // Draw 52 white keys.
  DrawKeys(canvas, size, /*is_black=*/false, white_key_w, white_key_w, piano_h,
           active_highlights, *font);

  // Draw 36 black keys.
  DrawKeys(canvas, size, /*is_black=*/true, white_key_w, black_key_w,
           black_key_h, active_highlights, *font);

  // Draw octave indicator bands.
  DrawOctaveBands(canvas, size, *band_font);
}

void Keyboard::DrawKeys(SkCanvas& canvas, const Size& size, bool is_black,
                        float white_key_w, float key_w, float key_h,
                        const std::map<int, uint32>& active_highlights,
                        const SkFont& font) {
  float piano_h = size.height - kOctaveBandsTotalHeight;
  for (int k = 0; k < kTotalPianoKeys; ++k) {
    if (IsKeyBlack(k) != is_black) continue;

    int w_idx = GetWhiteKeyCount(k);
    float x_left =
        is_black ? (w_idx * white_key_w - key_w / 2.0f) : (w_idx * white_key_w);

    DrawSingleKey(canvas, k, is_black, x_left, key_w, key_h, piano_h,
                  active_highlights, font);
  }
}

void Keyboard::DrawSingleKey(SkCanvas& canvas, int key_index, bool is_black,
                             float x_left, float key_w, float key_h,
                             float total_h,
                             const std::map<int, uint32>& active_highlights,
                             const SkFont& font) {
  bool is_pressed = pressed_keys_.count(key_index) > 0 ||
                    active_highlights.count(key_index) > 0;

  // Fill key background.
  SkColor bg_color = is_black ? kBlackKeyBgColor : kWhiteKeyBgColor;
  if (is_pressed) {
    auto it = active_highlights.find(key_index);
    if (it != active_highlights.end()) {
      bg_color = it->second;
    } else {
      bg_color = is_black ? kBlackKeyPressedColor : kWhiteKeyPressedColor;
    }
  }

  SkPaint fill_paint;
  fill_paint.setColor(bg_color);
  SkRect fill_rect =
      is_black ? SkRect::MakeXYWH(x_left, 0, key_w, key_h)
               : SkRect::MakeXYWH(x_left + 1.0f, 0, key_w - 2.0f, key_h);
  canvas.drawRect(fill_rect, fill_paint);

  // Stroke key border.
  SkPaint stroke_paint;
  stroke_paint.setColor(is_black ? kBlackKeyStrokeColor : kWhiteKeyStrokeColor);
  stroke_paint.setStyle(SkPaint::kStroke_Style);
  canvas.drawRect(SkRect::MakeXYWH(x_left, 0, key_w, key_h), stroke_paint);

  // Key binding overlay.
  std::string_view label = GetKeyLabel(key_index, bottom_octave_, top_octave_);
  if (!label.empty()) {
    SkPaint text_paint;
    SkColor text_color = is_black ? kBlackKeyLabelColor
                                  : (is_pressed ? kWhiteKeyPressedLabelColor
                                                : kWhiteKeyLabelColor);
    text_paint.setColor(text_color);
    text_paint.setAntiAlias(true);
    float text_w =
        font.measureText(label.data(), label.size(), SkTextEncoding::kUTF8);
    float text_x = x_left + (key_w - text_w) / 2.0f;
    float text_y = is_black ? (key_h - kBlackKeyLabelBottomPadding)
                            : (total_h - kWhiteKeyLabelBottomPadding);
    canvas.drawString(label.data(), text_x, text_y, font, text_paint);
  }
}

void Keyboard::DrawOctaveBands(SkCanvas& canvas, const Size& size,
                               const SkFont& font) {
  float piano_h = size.height - kOctaveBandsTotalHeight;
  float white_key_w = size.width / kTotalWhiteKeys;

  auto draw_band = [&](int octave, float y_top, SkColor fill_color,
                       SkColor stroke_color, std::string_view label) {
    int start_white_index = static_cast<int>(kOctave1CWhiteKeyOffset) +
                            (octave - 1) * kDiatonicStepsPerOctave;
    float x_left = start_white_index * white_key_w;
    float band_w = static_cast<float>(kDiatonicStepsPerOctave) * white_key_w;

    SkRect band_rect =
        SkRect::MakeXYWH(x_left, y_top, band_w, kOctaveBandRowHeight);

    SkPaint fill_paint;
    fill_paint.setColor(fill_color);
    canvas.drawRRect(SkRRect::MakeRectXY(band_rect, 2.0f, 2.0f), fill_paint);

    SkPaint stroke_paint;
    stroke_paint.setColor(stroke_color);
    stroke_paint.setStyle(SkPaint::kStroke_Style);
    stroke_paint.setStrokeWidth(1.0f);
    canvas.drawRRect(SkRRect::MakeRectXY(band_rect, 2.0f, 2.0f), stroke_paint);

    SkPaint text_paint;
    text_paint.setColor(kOctaveBandTextColor);
    text_paint.setAntiAlias(true);
    float text_w =
        font.measureText(label.data(), label.size(), SkTextEncoding::kUTF8);
    float text_x = x_left + (band_w - text_w) / 2.0f;
    float text_y = y_top + kOctaveBandRowHeight - 2.0f;
    canvas.drawString(label.data(), text_x, text_y, font, text_paint);
  };

  // Upper octave band (Q-U)
  float row1_y = piano_h + 2.0f;
  std::string upper_label = "Q-U (C" + std::to_string(top_octave_) + ")";
  draw_band(top_octave_, row1_y, kUpperBandFillColor, kUpperBandStrokeColor,
            upper_label);

  // Lower octave band (Z-M)
  float row2_y = row1_y + kOctaveBandRowHeight + kOctaveBandRowGap;
  std::string lower_label = "Z-M (C" + std::to_string(bottom_octave_) + ")";
  draw_band(bottom_octave_, row2_y, kLowerBandFillColor, kLowerBandStrokeColor,
            lower_label);
}

std::optional<int> Keyboard::MapKeyCodeToKeyIndex(
    perception::ui::KeyCode key_code) const {
  int semitone = -1;

  switch (key_code) {
    // Bottom Row (Z-M mapping to semitones 0..11)
    case perception::ui::KeyCode::Z:
      semitone = 0;
      break;
    case perception::ui::KeyCode::S:
      semitone = 1;
      break;
    case perception::ui::KeyCode::X:
      semitone = 2;
      break;
    case perception::ui::KeyCode::D:
      semitone = 3;
      break;
    case perception::ui::KeyCode::C:
      semitone = 4;
      break;
    case perception::ui::KeyCode::V:
      semitone = 5;
      break;
    case perception::ui::KeyCode::G:
      semitone = 6;
      break;
    case perception::ui::KeyCode::B:
      semitone = 7;
      break;
    case perception::ui::KeyCode::H:
      semitone = 8;
      break;
    case perception::ui::KeyCode::N:
      semitone = 9;
      break;
    case perception::ui::KeyCode::J:
      semitone = 10;
      break;
    case perception::ui::KeyCode::M:
      semitone = 11;
      break;

    // Top Row (Q-U mapping to semitones 12..23)
    case perception::ui::KeyCode::Q:
      semitone = 12;
      break;
    case perception::ui::KeyCode::Two:
      semitone = 13;
      break;
    case perception::ui::KeyCode::W:
      semitone = 14;
      break;
    case perception::ui::KeyCode::Three:
      semitone = 15;
      break;
    case perception::ui::KeyCode::E:
      semitone = 16;
      break;
    case perception::ui::KeyCode::R:
      semitone = 17;
      break;
    case perception::ui::KeyCode::Five:
      semitone = 18;
      break;
    case perception::ui::KeyCode::T:
      semitone = 19;
      break;
    case perception::ui::KeyCode::Six:
      semitone = 20;
      break;
    case perception::ui::KeyCode::Y:
      semitone = 21;
      break;
    case perception::ui::KeyCode::Seven:
      semitone = 22;
      break;
    case perception::ui::KeyCode::U:
      semitone = 23;
      break;

    default:
      return std::nullopt;
  }

  int target_octave;
  if (semitone >= 12) {
    semitone -= 12;
    target_octave = top_octave_;
  } else {
    target_octave = bottom_octave_;
  }
  int key_index = 3 + (target_octave - 1) * 12 + semitone;

  return std::clamp(key_index, 0, 87);
}

}  // namespace panels
