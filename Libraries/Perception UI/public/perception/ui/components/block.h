// Copyright 2022 Google LLC
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

#include "perception/ui/node.h"

namespace perception {
namespace ui {

struct DrawContext;

namespace components {

// A block is one of the fundemental building blocks for drawing something on
// the screen. It can have a border, be filled, can clip its contents, etc.
class Block {
 public:
  Block();
  void SetNode(std::weak_ptr<Node> node);

  void SetBorderColor(uint32 color);
  uint32 GetBorderColor() const;

  void SetBorderWidth(float width);
  double GetBorderWidth() const;

  void SetBorderRadius(float radius);
  double GetBorderRadius() const;

  void SetFillColor(uint32 color);
  uint32 GetFillColor() const;

  void SetClipContents(bool clip_contents);
  bool GetClipContents() const;

  void SetImageEffect(std::shared_ptr<ImageEffect> image_effect);
  std::shared_ptr<ImageEffect> GetImageEffect() const;

 private:
  void Draw(const DrawContext& draw_context);
  void DrawPostChildren(const DrawContext& draw_context);
  bool HitTest(const Point& point, const Size& size);
  bool HitTestAlongDimension(float value, float length, float &relative);

  void SetNeedsDraw();
  void SetNeedsDrawPostChildren();
  void SetNeedsHitTest();

  bool needs_draw_;
  bool needs_draw_post_children_;
  bool needs_hit_test_;

  std::weak_ptr<Node> node_;
  uint32 border_color_;
  float border_width_;
  float border_radius_;
  uint32 fill_color_;
  bool clip_contents_;
  std::shared_ptr<ImageEffect> image_effect_;

};

}  // namespace components
}  // namespace ui
}  // namespace perception
