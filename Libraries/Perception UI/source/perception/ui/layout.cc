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
#include "perception/ui/layout.h"

#include "yoga/Yoga.h"

namespace perception {
namespace ui {

Layout::Layout(YGNode* yoga_node) : yoga_node_(yoga_node) {}

void Layout::MarkDirty() { YGNodeMarkDirty(yoga_node_); }

void Layout::Calculate(float width, float height) {
  YGNodeCalculateLayout(yoga_node_, (float)width, (float)height,
                        YGDirectionLTR);
}

void Layout::CalculateIfDirty(float width, float height) {
  if (YGNodeIsDirty(yoga_node_)) {
    Calculate(width, height);
  }
}

void Layout::SetIsReferenceBaseline(int is_reference_baseline) {
  YGNodeSetIsReferenceBaseline(yoga_node_, is_reference_baseline);
}

bool Layout::IsReferenceBaseline() {
  return YGNodeIsReferenceBaseline(yoga_node_);
}

void Layout::SetHasNewLayout(bool has_new_layout) {
  YGNodeSetHasNewLayout(yoga_node_, has_new_layout);
}

bool Layout::HasNewLayout() { return YGNodeGetHasNewLayout(yoga_node_); }

void Layout::SetDirection(YGDirection direction) {
  YGNodeStyleSetDirection(yoga_node_, direction);
}

YGDirection Layout::GetDirection() {
  return YGNodeStyleGetDirection(yoga_node_);
}

YGDirection Layout::GetCalculatedDirection() {
  return YGNodeLayoutGetDirection(yoga_node_);
}

void Layout::SetFlexDirection(YGFlexDirection flex_direction) {
  YGNodeStyleSetFlexDirection(yoga_node_, flex_direction);
}

YGFlexDirection Layout::GetFlexDirection() {
  return YGNodeStyleGetFlexDirection(yoga_node_);
}

void Layout::SetJustifyContent(YGJustify justify_content) {
  YGNodeStyleSetJustifyContent(yoga_node_, justify_content);
}

YGJustify Layout::GetJustifyContent() {
  return YGNodeStyleGetJustifyContent(yoga_node_);
}

void Layout::SetAlignContent(YGAlign align_content) {
  YGNodeStyleSetAlignContent(yoga_node_, align_content);
}

YGAlign Layout::GetAlignContent() {
  return YGNodeStyleGetAlignContent(yoga_node_);
}

void Layout::SetAlignItems(YGAlign align_items) {
  YGNodeStyleSetAlignItems(yoga_node_, align_items);
}

YGAlign Layout::GetAlignItems() { return YGNodeStyleGetAlignItems(yoga_node_); }

void Layout::SetAlignSelf(YGAlign align_self) {
  YGNodeStyleSetAlignSelf(yoga_node_, align_self);
}

YGAlign Layout::GetAlignSelf() { return YGNodeStyleGetAlignSelf(yoga_node_); }

void Layout::SetPositionType(YGPositionType position_type) {
  YGNodeStyleSetPositionType(yoga_node_, position_type);
}

YGPositionType Layout::GetPositionType() {
  return YGNodeStyleGetPositionType(yoga_node_);
}

void Layout::SetFlexWrap(YGWrap flex_wrap) {
  YGNodeStyleSetFlexWrap(yoga_node_, flex_wrap);
}

YGWrap Layout::GetFlexWrap() { return YGNodeStyleGetFlexWrap(yoga_node_); }

void Layout::SetOverflow(YGOverflow overflow) {
  YGNodeStyleSetOverflow(yoga_node_, overflow);
}

YGOverflow Layout::GetOverflow() { return YGNodeStyleGetOverflow(yoga_node_); }

void Layout::SetDisplay(YGDisplay display) {
  YGNodeStyleSetDisplay(yoga_node_, display);
}

YGDisplay Layout::GetDisplay() { return YGNodeStyleGetDisplay(yoga_node_); }

void Layout::SetFlex(float flex) { YGNodeStyleSetFlex(yoga_node_, flex); }

float Layout::GetFlex() { return YGNodeStyleGetFlex(yoga_node_); }

void Layout::SetFlexGrow(float flex_grow) {
  YGNodeStyleSetFlexGrow(yoga_node_, flex_grow);
}

float Layout::GetFlexGrow() { return YGNodeStyleGetFlexGrow(yoga_node_); }

void Layout::SetFlexShrink(float flex_shrink) {
  YGNodeStyleSetFlexShrink(yoga_node_, flex_shrink);
}

float Layout::GetFlexShrink() { return YGNodeStyleGetFlexShrink(yoga_node_); }

void Layout::SetFlexBasis(float flex_basis) {
  YGNodeStyleSetFlexBasis(yoga_node_, flex_basis);
}

void Layout::SetFlexBasisPercent(float flex_basis) {
  YGNodeStyleSetFlexBasisPercent(yoga_node_, flex_basis);
}

void Layout::SetFlexBasisAuto() { YGNodeStyleSetFlexBasisAuto(yoga_node_); }

YGValue Layout::GetFlexBasis() { return YGNodeStyleGetFlexBasis(yoga_node_); }

void Layout::SetPosition(YGEdge edge, float position) {
  YGNodeStyleSetPosition(yoga_node_, edge, position);
}

void Layout::SetPositionPercent(YGEdge edge, float position) {
  YGNodeStyleSetPositionPercent(yoga_node_, edge, position);
}

YGValue Layout::GetPosition(YGEdge edge) {
  return YGNodeStyleGetPosition(yoga_node_, edge);
}

void Layout::SetMargin(YGEdge edge, float margin) {
  YGNodeStyleSetMargin(yoga_node_, edge, margin);
}

void Layout::SetMarginPercent(YGEdge edge, float margin) {
  YGNodeStyleSetMarginPercent(yoga_node_, edge, margin);
}

void Layout::SetMarginAuto(YGEdge edge) {
  YGNodeStyleSetMarginAuto(yoga_node_, edge);
}

YGValue Layout::GetMargin(YGEdge edge) {
  return YGNodeStyleGetMargin(yoga_node_, edge);
}

float Layout::GetComputedMargin(YGEdge edge) {
  return YGNodeLayoutGetMargin(yoga_node_, edge);
}

void Layout::SetPadding(YGEdge edge, float padding) {
  YGNodeStyleSetPadding(yoga_node_, edge, padding);
}

void Layout::SetPaddingPercent(YGEdge edge, float padding) {
  YGNodeStyleSetPaddingPercent(yoga_node_, edge, padding);
}

YGValue Layout::GetPadding(YGEdge edge) {
  return YGNodeStyleGetPadding(yoga_node_, edge);
}

float Layout::GetComputedPadding(YGEdge edge) {
  return YGNodeLayoutGetPadding(yoga_node_, edge);
}

void Layout::SetGap(float gap, YGGutter gutter) {
  YGNodeStyleSetGap(yoga_node_, gutter, gap);
}

float Layout::GetGap(YGEdge edge, YGGutter gutter) {
  return YGNodeStyleGetGap(yoga_node_, gutter);
}

void Layout::SetBorder(YGEdge edge, float border) {
  YGNodeStyleSetBorder(yoga_node_, edge, border);
}

float Layout::GetBorder(YGEdge edge) {
  return YGNodeStyleGetBorder(yoga_node_, edge);
}

float Layout::GetComputedBorder(YGEdge edge) {
  return YGNodeLayoutGetBorder(yoga_node_, edge);
}

void Layout::SetWidth(float width) { YGNodeStyleSetWidth(yoga_node_, width); }

void Layout::SetWidthPercent(float width) {
  YGNodeStyleSetWidthPercent(yoga_node_, width);
}

void Layout::SetWidthAuto() { YGNodeStyleSetWidthAuto(yoga_node_); }

YGValue Layout::GetWidth() { return YGNodeStyleGetWidth(yoga_node_); }

float Layout::GetCalculatedWidth() { return YGNodeLayoutGetWidth(yoga_node_); }

float Layout::GetCalculatedWidthWithMargin() {
  return GetCalculatedWidth() + GetComputedPadding(YGEdgeLeft) +
         GetComputedPadding(YGEdgeRight);
}

void Layout::SetHeight(float height) {
  YGNodeStyleSetHeight(yoga_node_, height);
}

void Layout::SetHeightPercent(float height) {
  YGNodeStyleSetHeightPercent(yoga_node_, height);
}

void Layout::SetHeightAuto() { YGNodeStyleSetHeightAuto(yoga_node_); }

YGValue Layout::GetHeight() { return YGNodeStyleGetHeight(yoga_node_); }

float Layout::GetCalculatedHeight() {
  return YGNodeLayoutGetHeight(yoga_node_);
}

float Layout::GetCalculatedHeightWithMargin() {
  return GetCalculatedHeight() + GetComputedPadding(YGEdgeTop) +
         GetComputedPadding(YGEdgeBottom);
}

void Layout::SetMinWidth(float min_width) {
  YGNodeStyleSetMinWidth(yoga_node_, min_width);
}

void Layout::SetMinWidthPercent(float min_height) {
  YGNodeStyleSetMinWidthPercent(yoga_node_, min_height);
}

YGValue Layout::GetMinWidth() { return YGNodeStyleGetMinWidth(yoga_node_); }

void Layout::SetMinHeight(float min_height) {
  YGNodeStyleSetMinHeight(yoga_node_, min_height);
}

void Layout::SetMinHeightPercent(float min_height) {
  YGNodeStyleSetMinHeightPercent(yoga_node_, min_height);
}

YGValue Layout::GetMinHeight() { return YGNodeStyleGetMinHeight(yoga_node_); }

void Layout::SetMaxWidth(float max_width) {
  YGNodeStyleSetMaxWidth(yoga_node_, max_width);
}

void Layout::SetMaxWidthPercent(float max_width) {
  YGNodeStyleSetMaxWidthPercent(yoga_node_, max_width);
}

YGValue Layout::GetMaxWidth() { return YGNodeStyleGetMaxWidth(yoga_node_); }

void Layout::SetMaxHeight(float max_height) {
  YGNodeStyleSetMaxHeight(yoga_node_, max_height);
}

void Layout::SetMaxHeightPercent(float max_height) {
  YGNodeStyleSetMaxHeightPercent(yoga_node_, max_height);
}

YGValue Layout::GetMaxHeight() { return YGNodeStyleGetMaxHeight(yoga_node_); }

void Layout::SetAspectRatio(float aspect_ratio) {
  YGNodeStyleSetAspectRatio(yoga_node_, aspect_ratio);
}

float Layout::GetAspectRatio() { return YGNodeStyleGetAspectRatio(yoga_node_); }

float Layout::GetLeft() { return YGNodeLayoutGetLeft(yoga_node_); }

float Layout::GetTop() { return YGNodeLayoutGetTop(yoga_node_); }

float Layout::GetRight() { return YGNodeLayoutGetRight(yoga_node_); }

float Layout::GetBottom() { return YGNodeLayoutGetBottom(yoga_node_); }

bool Layout::GetHadOverflow() { return YGNodeLayoutGetHadOverflow(yoga_node_); }

}  // namespace ui
}  // namespace perception
