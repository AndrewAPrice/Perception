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

#include "toasts.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

#include "compositor.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkString.h"
#include "include/core/SkSurface.h"
#include "perception/devices/graphics_device.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/shared_memory.h"
#include "perception/time.h"
#include "perception/ui/font.h"
#include "screen.h"
#include "window_manager.h"

namespace graphics = ::perception::devices::graphics;
using ::perception::AfterDuration;
using ::perception::Defer;
using ::perception::GetService;
using ::perception::GetTimeSinceKernelStarted;
using ::perception::SharedMemory;
using ::perception::devices::GraphicsDevice;
using ::perception::ui::Point;
using ::perception::ui::Rectangle;

namespace {

// Default unscaled width for a toast card.
constexpr float kToastUnscaledWidth = 280.0f;

// Margin from the right edge of the screen.
constexpr float kMarginRight = 16.0f;

// Margin from the top edge of the screen.
constexpr float kMarginTop = 16.0f;

// Vertical gap between stacked toasts.
constexpr float kStackGap = 10.0f;

// Duration before a toast expires automatically (in seconds).
constexpr std::chrono::seconds kToastDuration{5};

// Background color for toast cards.
constexpr SkColor kToastBackgroundColor = SkColorSetARGB(235, 32, 36, 44);

// Card background color components.
constexpr uint8 kToastBgRed = 32;
constexpr uint8 kToastBgGreen = 36;
constexpr uint8 kToastBgBlue = 44;

// Border color components.
constexpr uint8 kToastBorderRed = 70;
constexpr uint8 kToastBorderGreen = 78;
constexpr uint8 kToastBorderBlue = 92;

// Title text color components.
constexpr uint8 kToastTitleRed = 255;
constexpr uint8 kToastTitleGreen = 255;
constexpr uint8 kToastTitleBlue = 255;

// Text message color components.
constexpr uint8 kToastTextRed = 205;
constexpr uint8 kToastTextGreen = 210;
constexpr uint8 kToastTextBlue = 220;

// Maximum alpha values for toast elements.
constexpr float kToastMaxBgAlpha = 235.0f;
constexpr float kToastMaxBorderAlpha = 255.0f;
constexpr float kToastMaxTitleAlpha = 255.0f;
constexpr float kToastMaxTextAlpha = 205.0f;

// Toast dimensions and layout spacing.
constexpr float kToastMinUnscaledHeight = 48.0f;
constexpr float kToastBaseHeightPadding = 16.0f;
constexpr float kToastTitleHeight = 18.0f;
constexpr float kToastTextLineHeight = 16.0f;
constexpr float kToastNoTitlePadding = 4.0f;
constexpr float kToastCornerRadius = 8.0f;
constexpr float kToastBorderThickness = 1.0f;
constexpr float kToastHorizontalPadding = 12.0f;
constexpr float kToastTextTotalHorizontalMargin = 24.0f;
constexpr float kToastTopPadding = 8.0f;

// Animation thresholds and durations.
constexpr float kToastOpacityChangeThreshold = 0.005f;
constexpr float kToastMinDrawOpacity = 0.001f;
constexpr std::chrono::microseconds kFadeDuration{500000};
constexpr std::chrono::microseconds kSlideDuration{500000};

struct Toast {
  uint64 id;
  std::string title;
  std::string text;
  float unscaled_width = kToastUnscaledWidth;
  float unscaled_height = 0.0f;

  std::chrono::microseconds spawn_time;
  std::chrono::microseconds expire_time;
  std::chrono::microseconds fade_out_start_time{0};
  bool is_fading_out = false;

  // Vertical position animation
  float current_y = -1.0f;
  float start_y = -1.0f;
  float target_y = -1.0f;
  std::chrono::microseconds y_anim_start_time{0};
  bool is_y_animating = false;

  float current_opacity = -1.0f;

  size_t texture_id = 0;
  std::shared_ptr<SharedMemory> shared_memory;
  int texture_width = 0;
  int texture_height = 0;
  bool texture_dirty = true;

  Rectangle GetScreenBounds(float scale) const;
  float GetOpacity(std::chrono::microseconds now) const;
};

std::vector<std::unique_ptr<Toast>> g_toasts;
uint64 g_next_toast_id = 1;
Rectangle g_last_toasts_bounding_box;

Rectangle Toast::GetScreenBounds(float scale) const {
  auto screen_size = GetScreenSize();
  float margin_r = kMarginRight * scale;
  float w = unscaled_width * scale;
  float h = unscaled_height * scale;
  float y = current_y >= 0.0f ? current_y : kMarginTop * scale;
  float x = static_cast<float>(screen_size.width) - margin_r - w;
  return Rectangle{.origin = {x, y}, .size = {w, h}};
}

float Toast::GetOpacity(std::chrono::microseconds now) const {
  if (is_fading_out) {
    auto elapsed = now - fade_out_start_time;
    float progress =
        std::min(1.0f, static_cast<float>(elapsed.count()) /
                           static_cast<float>(kFadeDuration.count()));
    return 1.0f - progress;
  }
  if (now - spawn_time < kFadeDuration) {
    auto elapsed = now - spawn_time;
    return std::min(1.0f, static_cast<float>(elapsed.count()) /
                              static_cast<float>(kFadeDuration.count()));
  }
  return 1.0f;
}

Rectangle GetToastsBoundingBox() {
  if (g_toasts.empty()) return Rectangle{};

  float scale = WindowManager::GetScale();
  Rectangle box = g_toasts[0]->GetScreenBounds(scale);
  for (size_t i = 1; i < g_toasts.size(); i++) {
    box = box.Union(g_toasts[i]->GetScreenBounds(scale));
  }
  return box;
}

Toast* GetActiveToastAtPoint(const Point& point, float scale) {
  for (auto& toast_ptr : g_toasts) {
    if (toast_ptr->is_fading_out) continue;
    if (toast_ptr->GetScreenBounds(scale).Contains(point))
      return toast_ptr.get();
  }
  return nullptr;
}

std::vector<std::string> WrapText(const std::string& text, SkFont* font,
                                  float max_width) {
  std::vector<std::string> lines;
  if (!font || text.empty()) return lines;

  std::string current_line;
  std::string current_word;

  auto flush_word = [&](bool is_space) {
    if (current_word.empty()) return;

    std::string test_line = current_line.empty()
                                ? current_word
                                : (current_line + " " + current_word);
    SkScalar width = font->measureText(test_line.data(), test_line.length(),
                                       SkTextEncoding::kUTF8);

    if (width <= max_width) {
      current_line = test_line;
    } else {
      if (!current_line.empty()) {
        lines.push_back(current_line);
        current_line = current_word;
      } else {
        lines.push_back(current_word);
        current_line.clear();
      }
    }
    current_word.clear();
  };

  for (char c : text) {
    if (c == '\n') {
      flush_word(false);
      lines.push_back(current_line);
      current_line.clear();
    } else if (c == ' ') {
      flush_word(true);
    } else {
      current_word += c;
    }
  }
  flush_word(false);
  if (!current_line.empty()) lines.push_back(current_line);

  return lines;
}

float CalculateToastUnscaledHeight(const std::string& title,
                                   const std::string& text) {
  float text_max_width = kToastUnscaledWidth - kToastTextTotalHorizontalMargin;
  float height = kToastBaseHeightPadding;

  if (!title.empty()) height += kToastTitleHeight;

  if (!text.empty()) {
    SkFont* font = ::perception::ui::GetBook12UiFont();
    auto lines = WrapText(text, font, text_max_width);
    size_t line_count = std::max<size_t>(1, lines.size());
    height += static_cast<float>(line_count) * kToastTextLineHeight;
  }

  return std::max(height, kToastMinUnscaledHeight);
}

void DestroyToastTexture(Toast& toast) {
  if (toast.texture_id != 0) {
    GetService<GraphicsDevice>().DestroyTexture(
        graphics::TextureReference(toast.texture_id), [](Status) {});
    toast.texture_id = 0;
    toast.shared_memory.reset();
  }
}

void EnsureToastTexture(Toast& toast, float opacity) {
  if (!toast.texture_dirty && toast.texture_id != 0 &&
      std::abs(toast.current_opacity - opacity) <
          kToastOpacityChangeThreshold) {
    return;
  }

  toast.current_opacity = opacity;
  float scale = WindowManager::GetScale();
  int needed_w = static_cast<int>(std::round(toast.unscaled_width * scale));
  int needed_h = static_cast<int>(std::round(toast.unscaled_height * scale));
  if (needed_w <= 0 || needed_h <= 0) return;

  if (toast.texture_id == 0 || toast.texture_width != needed_w ||
      toast.texture_height != needed_h) {
    DestroyToastTexture(toast);

    graphics::CreateTextureRequest request;
    request.size = graphics::Size(needed_w, needed_h);
    auto status_or_res = GetService<GraphicsDevice>().CreateTexture(request);
    if (status_or_res) {
      toast.texture_id = status_or_res->texture.id;
      toast.shared_memory = status_or_res->pixel_buffer;
      toast.texture_width = needed_w;
      toast.texture_height = needed_h;
    } else {
      return;
    }
  }

#ifndef TEST
  if (!toast.shared_memory || !toast.shared_memory->Join()) return;

  auto image_info = SkImageInfo::Make(
      needed_w, needed_h, SkColorType::kBGRA_8888_SkColorType,
      SkAlphaType::kPremul_SkAlphaType, SkColorSpace::MakeSRGB());
  auto surface =
      SkSurfaces::WrapPixels(image_info, **toast.shared_memory, needed_w * 4);
  if (!surface) return;

  auto canvas = surface->getCanvas();
  canvas->clear(SK_ColorTRANSPARENT);

  SkRRect rrect;
  SkRect rect = SkRect::MakeWH(static_cast<float>(needed_w),
                               static_cast<float>(needed_h));
  rrect.setRectXY(rect, kToastCornerRadius * scale, kToastCornerRadius * scale);

  SkPaint paint;
  paint.setAntiAlias(true);

  int alpha_bg = static_cast<int>(std::round(kToastMaxBgAlpha * opacity));
  int alpha_border =
      static_cast<int>(std::round(kToastMaxBorderAlpha * opacity));
  int alpha_title = static_cast<int>(std::round(kToastMaxTitleAlpha * opacity));
  int alpha_text = static_cast<int>(std::round(kToastMaxTextAlpha * opacity));

  // Card Background
  paint.setStyle(SkPaint::kFill_Style);
  paint.setColor(
      SkColorSetARGB(alpha_bg, kToastBgRed, kToastBgGreen, kToastBgBlue));
  canvas->drawRRect(rrect, paint);

  // Border
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(kToastBorderThickness * scale);
  paint.setColor(SkColorSetARGB(alpha_border, kToastBorderRed,
                                kToastBorderGreen, kToastBorderBlue));
  canvas->drawRRect(rrect, paint);

  // Text Rendering
  float text_padding_left = kToastHorizontalPadding * scale;
  float text_max_width =
      (static_cast<float>(needed_w) - kToastTextTotalHorizontalMargin * scale) /
      scale;

  float current_y = kToastTopPadding * scale;

  if (!toast.title.empty()) {
    SkFont* bold_font = ::perception::ui::GetBold12UiFont();
    if (bold_font) {
      SkPaint title_paint;
      title_paint.setAntiAlias(true);
      title_paint.setColor(SkColorSetARGB(alpha_title, kToastTitleRed,
                                          kToastTitleGreen, kToastTitleBlue));

      canvas->save();
      canvas->scale(scale, scale);

      SkFontMetrics metrics;
      bold_font->getMetrics(&metrics);
      float title_y = (current_y / scale) - metrics.fAscent;
      canvas->drawString(SkString(toast.title.data(), toast.title.length()),
                         text_padding_left / scale, title_y, *bold_font,
                         title_paint);
      canvas->restore();

      current_y += kToastTitleHeight * scale;
    }
  } else {
    current_y += kToastNoTitlePadding * scale;
  }

  if (!toast.text.empty()) {
    SkFont* book_font = ::perception::ui::GetBook12UiFont();
    if (book_font) {
      SkPaint text_paint;
      text_paint.setAntiAlias(true);
      text_paint.setColor(SkColorSetARGB(alpha_text, kToastTextRed,
                                         kToastTextGreen, kToastTextBlue));

      auto lines = WrapText(toast.text, book_font, text_max_width);
      for (const auto& line : lines) {
        canvas->save();
        canvas->scale(scale, scale);

        SkFontMetrics metrics;
        book_font->getMetrics(&metrics);
        float msg_y = (current_y / scale) - metrics.fAscent;
        canvas->drawString(SkString(line.data(), line.length()),
                           text_padding_left / scale, msg_y, *book_font,
                           text_paint);
        canvas->restore();

        current_y += kToastTextLineHeight * scale;
      }
    }
  }
#endif

  toast.texture_dirty = false;
}

}  // namespace

void InvalidateAllToastsArea() {
  if (g_last_toasts_bounding_box.Width() > 0 &&
      g_last_toasts_bounding_box.Height() > 0) {
    InvalidateScreen(g_last_toasts_bounding_box);
  }
  g_last_toasts_bounding_box = GetToastsBoundingBox();
  if (g_last_toasts_bounding_box.Width() > 0 &&
      g_last_toasts_bounding_box.Height() > 0) {
    InvalidateScreen(g_last_toasts_bounding_box);
  }
}

void InitializeToasts() {}

void ShowToast(std::string_view title, std::string_view text) {
  auto toast = std::make_unique<Toast>();
  toast->id = g_next_toast_id++;
  toast->title = std::string(title);
  toast->text = std::string(text);
  toast->unscaled_height =
      CalculateToastUnscaledHeight(toast->title, toast->text);

  auto now = GetTimeSinceKernelStarted();
  toast->spawn_time = now;
  toast->expire_time =
      now +
      std::chrono::duration_cast<std::chrono::microseconds>(kToastDuration);
  toast->texture_dirty = true;

  g_toasts.push_back(std::move(toast));

  InvalidateAllToastsArea();

  AfterDuration(kToastDuration, []() { Defer([]() { UpdateToasts(); }); });
}

void UpdateToasts() {
  if (g_toasts.empty()) return;

  auto now = GetTimeSinceKernelStarted();
  float scale = WindowManager::GetScale();
  float margin_t = kMarginTop * scale;
  float gap = kStackGap * scale;
  bool has_animating = false;

  // Step 1: Update target Y positions for non-fading-out toasts
  float next_y = margin_t;
  for (size_t i = 0; i < g_toasts.size(); i++) {
    auto& toast = *g_toasts[i];
    if (!toast.is_fading_out) {
      if (toast.target_y < 0.0f) {
        // Initial placement
        toast.target_y = next_y;
        if (toast.current_y < 0.0f) toast.current_y = next_y;
      } else if (std::abs(toast.target_y - next_y) > 0.5f) {
        // Target Y shifted (slide up)
        toast.start_y = toast.current_y;
        toast.target_y = next_y;
        toast.y_anim_start_time = now;
        toast.is_y_animating = true;
      }

      float h = toast.unscaled_height * scale;
      next_y += h + gap;
    }
  }

  // Step 2: Animate position & opacity, check expiration & fade-out completion
  for (auto it = g_toasts.begin(); it != g_toasts.end();) {
    auto& toast = **it;

    // Auto-expire check
    if (!toast.is_fading_out && now >= toast.expire_time) {
      toast.is_fading_out = true;
      toast.fade_out_start_time = now;
    }

    if (toast.is_fading_out) {
      auto elapsed = now - toast.fade_out_start_time;
      if (elapsed >= kFadeDuration) {
        DestroyToastTexture(toast);
        it = g_toasts.erase(it);
        has_animating = true;
        continue;
      } else {
        has_animating = true;
      }
    } else if (now - toast.spawn_time < kFadeDuration) {
      has_animating = true;
    }

    // Position animation
    if (toast.is_y_animating) {
      auto elapsed = now - toast.y_anim_start_time;
      float progress =
          std::min(1.0f, static_cast<float>(elapsed.count()) /
                             static_cast<float>(kFadeDuration.count()));
      float smooth_t = progress * (2.0f - progress);  // Quadratic ease-out
      toast.current_y =
          toast.start_y + (toast.target_y - toast.start_y) * smooth_t;

      if (progress >= 1.0f) {
        toast.current_y = toast.target_y;
        toast.is_y_animating = false;
      } else {
        has_animating = true;
      }
    }

    ++it;
  }

  if (has_animating) InvalidateAllToastsArea();
}

void DrawToasts(const Rectangle& draw_area) {
  UpdateToasts();
  if (g_toasts.empty()) return;

  auto now = GetTimeSinceKernelStarted();
  float scale = WindowManager::GetScale();

  for (size_t i = 0; i < g_toasts.size(); i++) {
    auto& toast = *g_toasts[i];
    float opacity = toast.GetOpacity(now);
    if (opacity <= kToastMinDrawOpacity) continue;

    EnsureToastTexture(toast, opacity);
    if (toast.texture_id == 0) continue;

    Rectangle bounds = toast.GetScreenBounds(scale);
    auto intersection = draw_area.Intersection(bounds);
    if (!intersection) continue;

    Point offset = intersection->origin - bounds.origin;
    CopyAlphaBlendedTexture(*intersection, toast.texture_id, offset);
  }
}

bool HandleToastClick(const Point& mouse_position) {
  Toast* toast =
      GetActiveToastAtPoint(mouse_position, WindowManager::GetScale());
  if (!toast) return false;

  toast->is_fading_out = true;
  toast->fade_out_start_time = GetTimeSinceKernelStarted();
  InvalidateAllToastsArea();
  DrawScreen();
  return true;
}

bool IsMouseOverToast(const Point& point) {
  return GetActiveToastAtPoint(point, WindowManager::GetScale()) != nullptr;
}
