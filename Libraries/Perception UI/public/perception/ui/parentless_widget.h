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
#include <string>
#include <string_view>

#include "perception/ui/label.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/widget.h"

namespace perception {
namespace ui {

struct DrawContext;

class ParentlessWidget : public Widget {
 public:
  static std::shared_ptr<ParentlessWidget> Create(
      std::shared_ptr<Widget> contents);
  virtual ~ParentlessWidget();

  void SetParentSize(float width, float height);
  void MaybeRecalculateLayout();
  void InvalidateParentRenderHandler(
      const std::function<void()>& invalidate_parent_render_handler);
  void InvalidateParentLayoutHandler(
      const std::function<void()>& invalidate_parent_layout_handler);
  virtual void InvalidateRender() override;
  virtual void Draw(DrawContext& draw_context) override;

  virtual bool GetWidgetAt(float x, float y, std::shared_ptr<Widget>& widget,
                           float& x_in_selected_widget,
                           float& y_in_selected_widget) override;

  std::shared_ptr<Widget> GetContents();

 private:
  ParentlessWidget();

  static void LayoutDirtied(const YGNode* node);

  bool invalidated_;
  float width_, height_;
  std::function<void()> invalidate_parent_render_handler_;
  std::function<void()> invalidate_parent_layout_handler_;
  std::shared_ptr<Widget> contents_;
};

}  // namespace ui
}  // namespace perception
