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

#include "yoga/Yoga.h"

namespace perception {
namespace ui {

// Controls the layout of a widget.
// The layout uses Facebook Yoga. https://www.yogalayout.dev/docs/about-yoga
// Please see the excellent documentation here:
// https://www.yogalayout.dev/
class Layout {
 public:
  Layout(YGNode* yoga_node);
  void Calculate(float width, float height);
  void CalculateIfDirty(float width, float height);

  void MarkDirty();

  // Whether this widget should be considered the reference baseline among
  // siblings.
  void SetIsReferenceBaseline(int is_reference_baseline);
  bool IsReferenceBaseline();

  void SetHasNewLayout(bool has_new_layout);
  bool HasNewLayout();

  void SetDirection(YGDirection direction);
  YGDirection GetDirection();
  YGDirection GetCalculatedDirection();

  void SetFlexDirection(YGFlexDirection flex_direction);
  YGFlexDirection GetFlexDirection();

  void SetJustifyContent(YGJustify justify_content);
  YGJustify GetJustifyContent();

  void SetAlignContent(YGAlign align_content);
  YGAlign GetAlignContent();

  void SetAlignItems(YGAlign align_items);
  YGAlign GetAlignItems();

  void SetAlignSelf(YGAlign align_self);
  YGAlign GetAlignSelf();

  void SetPositionType(YGPositionType position_type);
  YGPositionType GetPositionType();

  void SetFlexWrap(YGWrap flex_wrap);
  YGWrap GetFlexWrap();

  void SetOverflow(YGOverflow overflow);
  YGOverflow GetOverflow();

  void SetDisplay(YGDisplay display);
  YGDisplay GetDisplay();

  void SetFlex(float flex);
  float GetFlex();

  void SetFlexGrow(float flex_grow);
  float GetFlexGrow();

  void SetFlexShrink(float flex_shrink);
  float GetFlexShrink();

  void SetFlexBasis(float flex_basis);
  void SetFlexBasisPercent(float flex_basis);
  void SetFlexBasisAuto();
  YGValue GetFlexBasis();

  void SetPosition(YGEdge edge, float position);
  void SetPositionPercent(YGEdge edge, float position);
  YGValue GetPosition(YGEdge edge);

  void SetMargin(YGEdge edge, float margin);
  void SetMarginPercent(YGEdge edge, float margin);
  void SetMarginAuto(YGEdge edge);
  YGValue GetMargin(YGEdge edge);
  float GetComputedMargin(YGEdge edge);

  void SetPadding(YGEdge edge, float padding);
  void SetPaddingPercent(YGEdge edge, float padding);
  YGValue GetPadding(YGEdge edge);
  float GetComputedPadding(YGEdge edge);

  void SetGap(float gap, YGGutter gutter = YGGutterAll);
  float GetGap(YGEdge edge, YGGutter gutter);

  void SetBorder(YGEdge edge, float border);
  float GetBorder(YGEdge edge);
  float GetComputedBorder(YGEdge edge);

  void SetWidth(float width);
  void SetWidthPercent(float width);
  void SetWidthAuto();
  YGValue GetWidth();
  float GetCalculatedWidth();
  float GetCalculatedWidthWithMargin();

  void SetHeight(float height);
  void SetHeightPercent(float height);
  void SetHeightAuto();
  YGValue GetHeight();
  float GetCalculatedHeight();
  float GetCalculatedHeightWithMargin();

  void SetMinWidth(float min_width);
  void SetMinWidthPercent(float min_height);
  YGValue GetMinWidth();

  void SetMinHeight(float min_height);
  void SetMinHeightPercent(float min_height);
  YGValue GetMinHeight();

  void SetMaxWidth(float max_width);
  void SetMaxWidthPercent(float max_width);
  YGValue GetMaxWidth();

  void SetMaxHeight(float max_height);
  void SetMaxHeightPercent(float max_height);
  YGValue GetMaxHeight();

  void SetAspectRatio(float aspect_ratio);
  float GetAspectRatio();

  float GetLeft();
  float GetTop();
  float GetRight();
  float GetBottom();
  bool GetHadOverflow();

 private:
  YGNode* yoga_node_;
};

}  // namespace ui
}  // namespace perception
