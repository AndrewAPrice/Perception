// Copyright 2024 Google LLC
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

#include <memory>
#include <functional>

#include "include/core/SkFont.h"
#include "perception/ui/point.h"
#include "perception/ui/size.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/node.h"
#include "yoga/Yoga.h"

namespace perception {
namespace ui {

struct DrawContext;
struct Rectangle;

namespace components {

// A bar that controls a scrollable area.
class ScrollBar {
 public:
 enum class Direction {
    VERTICAL,
    HORIZONTAL
 };

  ScrollBar();

  void SetNode(std::weak_ptr<Node> node);

  void SetDirection(Direction direction);
  Direction GetDirection() const;

  void OnScroll(std::function<void(float)> on_scroll_handler);

  void SetPosition(float minimum, float maximum, float value, float size);
  float GetPosition() const;

  void SetFont(SkFont* font);
  SkFont* GetFont() const;

  void SetColor(uint32_t color);
  uint32_t GetColor() const;

  void SetText(std::string_view text);
  std::string_view GetText() const;

  void SetTextAlignment(TextAlignment text_alignment);
  TextAlignment GetTextAlignment();

 private:
  Direction direction_;

  std::function<void(float)> on_scroll_handler_;
  bool is_mouse_hovering_over_track_;
  bool is_mouse_hovering_over_fab_;

  bool is_dragging_;
  float fab_drag_offset_;

  float minimum_;
  float maximum_;
  float value_;
  float size_;

Rectangle OffsetTrackToFabCoordiantes();
uint32 GetFabColor();

};

}  // namespace components
}  // namespace ui
}  // namespace perception