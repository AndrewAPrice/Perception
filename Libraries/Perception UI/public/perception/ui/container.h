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

#include "perception/ui/label.h"
#include "perception/ui/widget.h"

namespace perception {
namespace ui {

struct DrawContext;

class Container : public Widget {
 public:
  // Creates a standard container.
  static std::shared_ptr<Container> Create();

  virtual ~Container();

  Container* SetBorderColor(uint32 color);
  Container* SetBorderWidth(float width);
  Container* SetBorderRadius(float radius);
  Container* SetBackgroundColor(uint32 color);
  Container* SetClipContents(bool clip_contents);

  uint32 GetBorderColor() const;
  double GetBorderWidth() const;
  double GetBorderRadius() const;
  uint32 GetBackgroundColor() const;
  bool GetClipContents() const;

 protected:
  Container();
  virtual void Draw(DrawContext& draw_context) override;

  virtual void DrawContents(DrawContext& draw_context);

  uint32 border_color_;
  float border_width_;
  float border_radius_;
  uint32 background_color_;
  bool clip_contents_;
};

}  // namespace ui
}  // namespace perception
