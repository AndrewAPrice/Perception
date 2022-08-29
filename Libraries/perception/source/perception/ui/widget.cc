// Copyright 2021 Google LLC
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

#include "perception/ui/widget.h"

#include "perception/ui/draw_context.h"

namespace perception {
namespace ui {

Widget::Widget() :
	layout_dirtied_(true), yoga_node_(YGNodeNew()) {
    YGNodeSetContext(yoga_node_, this);
    YGNodeSetDirtiedFunc(yoga_node_, &Widget::LayoutDirtied);
}

Widget::~Widget() {
    // This does some extra work than merely deleting the node.
    YGNodeFree(yoga_node_);
}

std::weak_ptr<Widget> Widget::GetParent() {
	return parent_;
}

void Widget::SetParent(std::weak_ptr<Widget> parent) {
	parent_ = parent;
}

void Widget::ClearParent() {
	parent_.reset();
}


Widget* Widget::AddChildren(
    const std::vector<std::shared_ptr<Widget>>& children) {
    for (auto child : children) {
        AddChild(child);
    }
    return this;
}

Widget* Widget::AddChild(std::shared_ptr<Widget> child) {
    child->SetParent(ToSharedPtr());
    YGNodeInsertChild(yoga_node_, child->yoga_node_,
        children_.size());
    children_.push_back(child);
    return this;
}

Widget* Widget::RemoveChild(std::shared_ptr<Widget> child) {
    child->ClearParent();
    children_.remove(child);
    YGNodeRemoveChild(yoga_node_, child->yoga_node_);
    return this;
}

Widget* Widget::RemoveChildren() {
    for (auto child : children_) {
        child->ClearParent();
    }
    children_.clear();
    YGNodeRemoveAllChildren(yoga_node_);
    return this;
}

const std::list<std::shared_ptr<Widget>>& Widget::GetChildren() {
    return children_;
}

Widget* Widget::SetIsReferenceBaseline(int is_reference_baseline) {
    YGNodeSetIsReferenceBaseline(yoga_node_, is_reference_baseline);
    return this;
}

bool Widget::IsReferenceBaseline() {
    return YGNodeIsReferenceBaseline(yoga_node_);
}

Widget* Widget::SetHasNewLayout(bool has_new_layout) {
    YGNodeSetHasNewLayout(yoga_node_, has_new_layout);
    return this;
}

bool Widget::HasNewLayout() {
    return YGNodeGetHasNewLayout(yoga_node_);
}

Widget* Widget::SetDirection(YGDirection direction) {
    YGNodeStyleSetDirection(yoga_node_, direction);
    return this;
}

YGDirection Widget::GetDirection() {
    return YGNodeStyleGetDirection(yoga_node_);
}

YGDirection Widget::GetCalculatedDirection() {
    return YGNodeLayoutGetDirection(yoga_node_);
}

Widget* Widget::SetFlexDirection(YGFlexDirection flex_direction) {
    YGNodeStyleSetFlexDirection(yoga_node_, flex_direction);
    return this;
}

YGFlexDirection Widget::GetFlexDirection() {
    return YGNodeStyleGetFlexDirection(yoga_node_);
}

Widget* Widget::SetJustifyContent(YGJustify justify_content) {
    YGNodeStyleSetJustifyContent(yoga_node_, justify_content);
    return this;
}

YGJustify Widget::GetJustifyContent() {
    return YGNodeStyleGetJustifyContent(yoga_node_);
}

Widget* Widget::SetAlignContent(YGAlign align_content) {
    YGNodeStyleSetAlignContent(yoga_node_, align_content);
    return this;
}

YGAlign Widget::GetAlignContent() {
    return YGNodeStyleGetAlignContent(yoga_node_);
}

Widget* Widget::SetAlignItems(YGAlign align_items) {
    YGNodeStyleSetAlignItems(yoga_node_, align_items);
    return this;
}

YGAlign Widget::GetAlignItems() {
    return YGNodeStyleGetAlignItems(yoga_node_);
}

Widget* Widget::SetAlignSelf(YGAlign align_self) {
    YGNodeStyleSetAlignSelf(yoga_node_, align_self);
    return this;
}

YGAlign Widget::GetAlignSelf() {
    return YGNodeStyleGetAlignSelf(yoga_node_);
}

Widget* Widget::SetPositionType(YGPositionType position_type) {
    YGNodeStyleSetPositionType(yoga_node_, position_type);
    return this;
}

YGPositionType Widget::GetPositionType() {
    return YGNodeStyleGetPositionType(yoga_node_);
}

Widget* Widget::SetFlexWrap(YGWrap flex_wrap) {
    YGNodeStyleSetFlexWrap(yoga_node_, flex_wrap);
    return this;
}

YGWrap Widget::GetFlexWrap() {
    return YGNodeStyleGetFlexWrap(yoga_node_);
}

Widget* Widget::SetOverflow(YGOverflow overflow) {
    YGNodeStyleSetOverflow(yoga_node_, overflow);
    return this;
}

YGOverflow Widget::GetOverflow() {
    return YGNodeStyleGetOverflow(yoga_node_);
}

Widget* Widget::SetDisplay(YGDisplay display) {
    YGNodeStyleSetDisplay(yoga_node_, display);
    return this;
}

YGDisplay Widget::GetDisplay() {
    return YGNodeStyleGetDisplay(yoga_node_);
}

Widget* Widget::SetFlex(float flex) {
    YGNodeStyleSetFlex(yoga_node_, flex);
    return this;
}

float Widget::GetFlex() {
    return YGNodeStyleGetFlex(yoga_node_);
}

Widget* Widget::SetFlexGrow(float flex_grow) {
    YGNodeStyleSetFlexGrow(yoga_node_, flex_grow);
    return this;
}

float Widget::GetFlexGrow() {
    return YGNodeStyleGetFlexGrow(yoga_node_);
}

Widget* Widget::SetFlexShrink(float flex_shrink) {
    YGNodeStyleSetFlexShrink(yoga_node_, flex_shrink);
    return this;
}

float Widget::GetFlexShrink() {
    return YGNodeStyleGetFlexShrink(yoga_node_);
}

Widget* Widget::SetFlexBasis(float flex_basis) {
    YGNodeStyleSetFlexBasis(yoga_node_, flex_basis);
    return this;
}

Widget* Widget::SetFlexBasisPercent(float flex_basis) {
    YGNodeStyleSetFlexBasisPercent(yoga_node_, flex_basis);
    return this;
}

Widget* Widget::SetFlexBasisAuto() {
    YGNodeStyleSetFlexBasisAuto(yoga_node_);
    return this;
}

YGValue Widget::GetFlexBasis() {
    return YGNodeStyleGetFlexBasis(yoga_node_);
}

Widget* Widget::SetPosition(YGEdge edge, float position) {
    YGNodeStyleSetPosition(yoga_node_, edge, position);
    return this;
}

Widget* Widget::SetPositionPercent(YGEdge edge, float position) {
    YGNodeStyleSetPositionPercent(yoga_node_, edge, position);
    return this;
}

YGValue Widget::GetPosition(YGEdge edge) {
    return YGNodeStyleGetPosition(yoga_node_, edge);
}

Widget* Widget::SetMargin(YGEdge edge, float margin) {
    YGNodeStyleSetMargin(yoga_node_, edge, margin);
    return this;
}

Widget* Widget::SetMarginPercent(YGEdge edge, float margin) {
    YGNodeStyleSetMarginPercent(yoga_node_, edge, margin);
    return this;
}

Widget* Widget::SetMarginAuto(YGEdge edge) {
    YGNodeStyleSetMarginAuto(yoga_node_, edge);
    return this;
}

YGValue Widget::GetMargin(YGEdge edge) {
    return YGNodeStyleGetMargin(yoga_node_, edge);
}

float Widget::GetComputedMargin(YGEdge edge) {
    return YGNodeLayoutGetMargin(yoga_node_, edge);
}

Widget* Widget::SetPadding(YGEdge edge, float padding) {
    YGNodeStyleSetPadding(yoga_node_, edge, padding);
    return this;
}

Widget* Widget::SetPaddingPercentage(YGEdge edge, float padding) {
    YGNodeStyleSetPaddingPercent(yoga_node_, edge, padding);
    return this;
}

YGValue Widget::GetPadding(YGEdge edge) {
    return YGNodeStyleGetPadding(yoga_node_, edge);
}

float Widget::GetComputedPadding(YGEdge edge) {
    return YGNodeLayoutGetPadding(yoga_node_, edge);
}

Widget* Widget::SetBorder(YGEdge edge, float border) {
    YGNodeStyleSetBorder(yoga_node_, edge, border);
    return this;
}

float Widget::GetBorder(YGEdge edge) {
    return YGNodeStyleGetBorder(yoga_node_, edge);
}

float Widget::GetComputedBorder(YGEdge edge) {
    return YGNodeLayoutGetBorder(yoga_node_, edge);
}

Widget* Widget::SetWidth(float width) {
    YGNodeStyleSetWidth(yoga_node_, width);
    return this;
}

Widget* Widget::SetWidthPercent(float width) {
    YGNodeStyleSetWidthPercent(yoga_node_, width);
    return this;
}

Widget* Widget::SetWidthAuto() {
    YGNodeStyleSetWidthAuto(yoga_node_);
    return this;
}

YGValue Widget::GetWidth() {
    return YGNodeStyleGetWidth(yoga_node_);
}

float Widget::GetCalculatedWidth() {
    return YGNodeLayoutGetWidth(yoga_node_);
}

Widget* Widget::SetHeight(float height) {
    YGNodeStyleSetHeight(yoga_node_, height);
    return this;
}

Widget* Widget::SetHeightPercent(float height) {
    YGNodeStyleSetHeightPercent(yoga_node_, height);
    return this;
}

Widget* Widget::SetHeightAuto() {
    YGNodeStyleSetHeightAuto(yoga_node_);
    return this;
}

YGValue Widget::GetHeight() {
    return YGNodeStyleGetHeight(yoga_node_);
}

float Widget::GetCalculatedHeight() {
    return YGNodeLayoutGetHeight(yoga_node_);
}

Widget* Widget::SetMinWidth(float min_width) {
    YGNodeStyleSetMinWidth(yoga_node_, min_width);
    return this;
}

Widget* Widget::SetMinWidthPercent(float min_height) {
    YGNodeStyleSetMinWidthPercent(yoga_node_, min_height);
    return this;
}

YGValue Widget::GetMinWidth() {
    return YGNodeStyleGetMinWidth(yoga_node_);
}

Widget* Widget::SetMinHeight(float min_height) {
    YGNodeStyleSetMinHeight(yoga_node_, min_height);
    return this;
}

Widget* Widget::SetMinHeightPercent(float min_height) {
    YGNodeStyleSetMinHeightPercent(yoga_node_, min_height);
    return this;
}

YGValue Widget::GetMinHeight() {
    return YGNodeStyleGetMinHeight(yoga_node_);
}

Widget* Widget::SetMaxWidth(float max_width) {
    YGNodeStyleSetMaxWidth(yoga_node_, max_width);
    return this;
}

Widget* Widget::SetMaxWidthPercent(float max_width) {
    YGNodeStyleSetMaxWidthPercent(yoga_node_, max_width);
    return this;
}

YGValue Widget::GetMaxWidth() {
    return YGNodeStyleGetMaxWidth(yoga_node_);
}

Widget* Widget::SetMaxHeight(float max_height) {
    YGNodeStyleSetMaxHeight(yoga_node_, max_height);
    return this;
}

Widget* Widget::SetMaxHeightPercent(float max_height) {
    YGNodeStyleSetMaxHeightPercent(yoga_node_, max_height);
    return this;
}

YGValue Widget::GetMaxHeight() {
    return YGNodeStyleGetMaxHeight(yoga_node_);
}

Widget* Widget::SetAspectRatio(float aspect_ratio) {
    YGNodeStyleSetAspectRatio(yoga_node_, aspect_ratio);
    return this;
}

float Widget::GetAspectRatio() {
    return YGNodeStyleGetAspectRatio(yoga_node_);
}

float Widget::GetLeft() {
    return YGNodeLayoutGetLeft(yoga_node_);
}

float Widget::GetTop() {
    return YGNodeLayoutGetTop(yoga_node_);
}

float Widget::GetRight() {
    return YGNodeLayoutGetRight(yoga_node_);
}

float Widget::GetBottom() {
    return YGNodeLayoutGetBottom(yoga_node_);
}

bool Widget::GetHadOverflow() {
    return YGNodeLayoutGetHadOverflow(yoga_node_);
}

bool Widget::GetDidLegacyStretchFlagAffectLayout() {
    return YGNodeLayoutGetDidLegacyStretchFlagAffectLayout(yoga_node_);
}

void Widget::Draw(DrawContext& draw_context) {
    float old_offset_x = draw_context.offset_x;
    float old_offset_y = draw_context.offset_y;
    draw_context.offset_x += GetLeft();
    draw_context.offset_y += GetTop();
    for (auto& child : children_)
        child->Draw(draw_context);
}

bool Widget::GetWidgetAt(float x, float y,
    std::shared_ptr<Widget>& widget,
    float& x_in_selected_widget,
    float& y_in_selected_widget) {

    if (x >= GetLeft() && x < GetRight() &&
        y >= GetTop() && y < GetBottom()) {
        // Walk backwards (from top to bottom).
        for (auto itr = children_.rbegin(); itr != children_.rend(); itr++) {
            if ((*itr)->GetWidgetAt(x, y, widget,
                x_in_selected_widget, y_in_selected_widget))
                return true;
        }
        // None of our children.
        return false;
    } else {
        // Outside our bounds.
        return false;
    }
}

void Widget::OnMouseEnter() { }
void Widget::OnMouseLeave() {}
void Widget::OnMouseMove(float x, float y) {}
void Widget::OnMouseButtonDown(float x, float y,
    ::permebuf::perception::devices::MouseButton button) {}
void Widget::OnMouseButtonUp(float x, float y,
    ::permebuf::perception::devices::MouseButton button) {}

void Widget::InvalidateRender() {
    if (auto parent = parent_.lock()) {
       parent->InvalidateRender();
    }
}

void Widget::LayoutDirtied(YGNode* node) {
    Widget* widget = (Widget*) YGNodeGetContext(node);
    widget->InvalidateRender();
}

}
}
