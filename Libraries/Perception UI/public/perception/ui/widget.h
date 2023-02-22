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

#pragma once

#include <list>
#include <memory>

#include "permebuf/Libraries/perception/devices/mouse_listener.permebuf.h"
#include "yoga/Yoga.h"

namespace perception {
namespace ui {

constexpr int kFillParent = -1;
constexpr int kFitContent = -2;

class DrawContext;

class Widget : public std::enable_shared_from_this<Widget> {
 public:
  Widget();
  ~Widget();
  std::shared_ptr<Widget> ToSharedPtr() { return shared_from_this(); }

  std::weak_ptr<Widget> GetParent();
  void SetParent(std::weak_ptr<Widget> parent);
  void ClearParent();

  Widget* AddChildren(const std::vector<std::shared_ptr<Widget>>& children);
  Widget* AddChild(std::shared_ptr<Widget> child);
  Widget* RemoveChild(std::shared_ptr<Widget> child);
  Widget* RemoveChildren();
  const std::list<std::shared_ptr<Widget>>& GetChildren();

  Widget* SetIsReferenceBaseline(int is_reference_baseline);
  bool IsReferenceBaseline();

  Widget* SetHasNewLayout(bool has_new_layout);
  bool HasNewLayout();

  Widget* SetDirection(YGDirection direction);
  YGDirection GetDirection();
  YGDirection GetCalculatedDirection();

  Widget* SetFlexDirection(YGFlexDirection flex_direction);
  YGFlexDirection GetFlexDirection();

  Widget* SetJustifyContent(YGJustify justify_content);
  YGJustify GetJustifyContent();

  Widget* SetAlignContent(YGAlign align_content);
  YGAlign GetAlignContent();

  Widget* SetAlignItems(YGAlign align_items);
  YGAlign GetAlignItems();

  Widget* SetAlignSelf(YGAlign align_self);
  YGAlign GetAlignSelf();

  Widget* SetPositionType(YGPositionType position_type);
  YGPositionType GetPositionType();

  Widget* SetFlexWrap(YGWrap flex_wrap);
  YGWrap GetFlexWrap();

  Widget* SetOverflow(YGOverflow overflow);
  YGOverflow GetOverflow();

  Widget* SetDisplay(YGDisplay display);
  YGDisplay GetDisplay();

  Widget* SetFlex(float flex);
  float GetFlex();

  Widget* SetFlexGrow(float flex_grow);
  float GetFlexGrow();

  Widget* SetFlexShrink(float flex_shrink);
  float GetFlexShrink();

  Widget* SetFlexBasis(float flex_basis);
  Widget* SetFlexBasisPercent(float flex_basis);
  Widget* SetFlexBasisAuto();
  YGValue GetFlexBasis();

  Widget* SetPosition(YGEdge edge, float position);
  Widget* SetPositionPercent(YGEdge edge, float position);
  YGValue GetPosition(YGEdge edge);

  Widget* SetMargin(YGEdge edge, float margin);
  Widget* SetMarginPercent(YGEdge edge, float margin);
  Widget* SetMarginAuto(YGEdge edge);
  YGValue GetMargin(YGEdge edge);
  float GetComputedMargin(YGEdge edge);

  Widget* SetPadding(YGEdge edge, float padding);
  Widget* SetPaddingPercentage(YGEdge edge, float padding);
  YGValue GetPadding(YGEdge edge);
  float GetComputedPadding(YGEdge edge);

  Widget* SetBorder(YGEdge edge, float border);
  float GetBorder(YGEdge edge);
  float GetComputedBorder(YGEdge edge);

  Widget* SetWidth(float width);
  Widget* SetWidthPercent(float width);
  Widget* SetWidthAuto();
  YGValue GetWidth();
  float GetCalculatedWidth();
  float GetCalculatedWidthWithMargin();

  Widget* SetHeight(float height);
  Widget* SetHeightPercent(float height);
  Widget* SetHeightAuto();
  YGValue GetHeight();
  float GetCalculatedHeight();
  float GetCalculatedHeightWithMargin();

  Widget* SetMinWidth(float min_width);
  Widget* SetMinWidthPercent(float min_height);
  YGValue GetMinWidth();

  Widget* SetMinHeight(float min_height);
  Widget* SetMinHeightPercent(float min_height);
  YGValue GetMinHeight();

  Widget* SetMaxWidth(float max_width);
  Widget* SetMaxWidthPercent(float max_width);
  YGValue GetMaxWidth();

  Widget* SetMaxHeight(float max_height);
  Widget* SetMaxHeightPercent(float max_height);
  YGValue GetMaxHeight();

  Widget* SetAspectRatio(float aspect_ratio);
  float GetAspectRatio();

  float GetLeft();
  float GetTop();
  float GetRight();
  float GetBottom();
  bool GetHadOverflow();

  Widget* SetId(size_t id);
  size_t GetId() const;
  static std::weak_ptr<Widget> GetWidgetWithId(size_t id);

  // The below functions are not intended for end users unless
  // you are building widgets.
  virtual void Draw(DrawContext& draw_context);

  // Gets the widget at X/Y.
  // If x,y points to a selectable widget, sets widget, x_in_selected_widget,
  // y_in_selected_widget and returns true.
  // If the mouse is within the bounds of this widget, but this widget isn't
  // selectable, sets widget to nullptr, but returns true.
  // If the mouse is outside of the bounds of this widget, returns false, and
  // doesn't touch the mutable parameters.
  virtual bool GetWidgetAt(float x, float y, std::shared_ptr<Widget>& widget,
                           float& x_in_selected_widget,
                           float& y_in_selected_widget);

  virtual void OnMouseEnter();
  virtual void OnMouseLeave();
  virtual void OnMouseMove(float x, float y);
  virtual void OnMouseButtonDown(
      float x, float y, ::permebuf::perception::devices::MouseButton button);
  virtual void OnMouseButtonUp(
      float x, float y, ::permebuf::perception::devices::MouseButton button);

  YGNode *GetYogaNode();

 protected:
  virtual void InvalidateRender();

  std::weak_ptr<Widget> parent_;
  std::list<std::shared_ptr<Widget>> children_;
  size_t id_;

  YGNode* yoga_node_;
  bool layout_dirtied_;

 private:
  static void LayoutDirtied(YGNode* node);
};

}  // namespace ui
}  // namespace perception