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

std::shared_ptr<ParentlessWidget> ParentlessWidget::Create(
    std::shared_ptr<Widget> contents) {
  std::shared_ptr<ParentlessWidget> parentless_widget(new ParentlessWidget());
  parentless_widget->contents_ = contents;
  parentless_widget->AddChild(contents);
  return parentless_widget;
}

ParentlessWidget::~ParentlessWidget() {}

void ParentlessWidget::SetParentSize(float width, float height) {
  if (width == width_ && height == height_) return;

  invalidated_ = true;
  width_ = width;
  height_ = height;
}

void ParentlessWidget::InvalidateParentRenderHandler(
    const std::function<void()>& invalidate_parent_render_handler) {
  invalidate_parent_render_handler_ = invalidate_parent_render_handler;
}

void ParentlessWidget::InvalidateParentLayoutHandler(
    const std::function<void()>& invalidate_parent_layout_handler) {
  invalidate_parent_layout_handler_ = invalidate_parent_layout_handler;
}

void ParentlessWidget::InvalidateRender() {
  invalidated_ = true;
  if (invalidate_parent_render_handler_) invalidate_parent_render_handler_();
}

void ParentlessWidget::Draw(DrawContext& draw_context) {
  if (YGNodeIsDirty(yoga_node_) || invalidated_) {
    YGNodeCalculateLayout(yoga_node_, (float)width_, (float)height_,
                          YGDirectionLTR);
    invalidated_ = false;
  }
  contents_->Draw(draw_context);
}

bool ParentlessWidget::GetWidgetAt(float x, float y,
                                   std::shared_ptr<Widget>& widget,
                                   float& x_in_selected_widget,
                                   float& y_in_selected_widget) {
  // Forward straight into the contents without clipping against us. If we are
  // the child of a scroll container, then often this widget is a small window
  // into much bigger contents, and clipping against us will make it impossible
  // to select items outside of our 'window'.
  return contents_->GetWidgetAt(x, y, widget, x_in_selected_widget,
                                y_in_selected_widget);
}

std::shared_ptr<Widget> ParentlessWidget::GetContents() { return contents_; }

ParentlessWidget::ParentlessWidget()
    : invalidated_(true), width_(0), height_(0) {
  YGNodeSetDirtiedFunc(yoga_node_, &ParentlessWidget::LayoutDirtied);
}

void ParentlessWidget::MaybeRecalculateLayout() {
  if (!invalidated_) return;

  YGNodeCalculateLayout(yoga_node_, width_, height_, YGDirectionLTR);
}

void ParentlessWidget::LayoutDirtied(YGNode* node) {
  ParentlessWidget* parentless_widget =
      (ParentlessWidget*)YGNodeGetContext(node);
  if (parentless_widget->invalidate_parent_layout_handler_)
    parentless_widget->invalidate_parent_layout_handler_();
}

}  // namespace ui
}  // namespace perception