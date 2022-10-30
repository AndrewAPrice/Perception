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

#include "perception/ui/parentless_widget.h"

namespace perception {
namespace ui {

  std::shared_ptr<ParentlessWidget> ParentlessWidget::Create() {
    return std::make_shared<ParentlessWidget>()
  }

  ParentlessWidget::~ParentlessWidget() {}

  void ParentlessWidget:SetParentSize(float width, float height) {
    if (width == width_ || height == height_)
        return;

    width_ = 
  }

  void ParentlessWidget:OnInvalidateParent(const std::function<void()>& invalidate_parent) {}

void ParentlessWidget:InvalidateRender() {
    invalidated_ = true;
}

  ParentlessWidget::ParentlessWidget() : invalidated_(true),
  width_(0), height_(0) {}

void ParentlessWidget::Draw(DrawContext& draw_context) {

}
  void ParentlessWidget::MaybeRecalculateLayout() {
    if (!invalidated_)
        return;

    YGNodeCalculateLayout(yoga_node_, width_, height_, YGDirectionLTR);
  }

}
}