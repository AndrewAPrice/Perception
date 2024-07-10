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

#include <memory>
#include <string_view>

#include "perception/ui/builders/macros.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "yoga/Yoga.h"

namespace perception {
namespace ui {
namespace builders {

// Creates a text node.
template <typename... Modifiers>
std::shared_ptr<perception::ui::Node> Node(Modifiers... modifiers) {
  auto node = std::make_shared<perception::ui::Node>();
  ApplyModifiersToNode(*node, modifiers...);
  return std::move(node);
};

// Modifier to set that this node should be considered the reference baseline
// among siblings.
LAYOUT_MODIFIER(IsReferenceBaseline, SetIsReferenceBaseline, true);

// Modifier to set that this node should be left-to-right.
LAYOUT_MODIFIER(LeftToRight, SetDirection, YGDirectionLTR);

// Modifier to set that this node should be right-to-left.
LAYOUT_MODIFIER(RightToLeft, SetDirection, YGDirectionRTL);

// Modifier to set the flex direction.
LAYOUT_MODIFIER_1(FlexDirection, SetFlexDirection, YGFlexDirection);

// Modifier to set the content justification.
LAYOUT_MODIFIER_1(JustifyContent, SetJustifyContent, YGJustify);

// Modifier to set the content alignment.
LAYOUT_MODIFIER_1(AlignContent, SetAlignContent, YGAlign);

// Modifier to set the child item alignment.
LAYOUT_MODIFIER_1(AlignItems, SetAlignItems, YGAlign);

// Modifier to set this node's alignment.
LAYOUT_MODIFIER_1(AlignSelf, SetAlignSelf, YGAlign);

// Modifier to set this node's position type.
LAYOUT_MODIFIER_1(PositionType, SetPositionType, YGPositionType);

// Modifier to set this node's flex wrap.
LAYOUT_MODIFIER_1(FlexWrap, SetFlexWrap, YGWrap);

// Modifier to hide this node.
LAYOUT_MODIFIER(Hide, SetDisplay, YGDisplayNone);

// Modifier to show this node.
LAYOUT_MODIFIER(Show, SetDisplay, YGDisplayFlex);

// Modifier to set this node's flex.
LAYOUT_MODIFIER_1(Flex, SetFlex, float);

// Modifier to set this node's flex grow.
LAYOUT_MODIFIER_1(FlexGrow, SetFlexGrow, float);

// Modifier to set this node's flex shrink.
LAYOUT_MODIFIER_1(FlexShrink, SetFlexShrink, float);

// Modifier to set this node's flex basis to an absolute number.
LAYOUT_MODIFIER_1(FlexBasis, SetFlexBasis, float);

// Modifier to set this node's flex basis to a percentage.
LAYOUT_MODIFIER_1(FlexBasisPercent, SetFlexBasisPercent, float);

// Modifier to set this node's flex basis to auto.
LAYOUT_MODIFIER(FlexBasisAuto, SetFlexBasisAuto, );

// Modifier to set this node's position to an absolute number.
LAYOUT_MODIFIER_2(Position, SetPosition, YGEdge, float);

// Modifier to set this node's position to a percentage.
LAYOUT_MODIFIER_2(PositionPercent, SetPositionPercent, YGEdge, float);

// Modifier to set this node's margin to an absolute number.
LAYOUT_MODIFIER_2(Margin, SetMargin, YGEdge, float);

// Modifier to set this node's margin to a percentage.
LAYOUT_MODIFIER_2(MarginPercent, SetMarginPercent, YGEdge, float);

// Modifier to set this node's margin to auto.
LAYOUT_MODIFIER_1(MarginAuto, SetMarginAuto, YGEdge);

// Modifier to set this node's padding to an absolute number.
LAYOUT_MODIFIER_2(Padding, SetPadding, YGEdge, float);

// Modifier to set this node's padding to a percentage.
LAYOUT_MODIFIER_2(PaddingPercent, SetPaddingPercent, YGEdge, float);

// Modifier to set this node's border.
LAYOUT_MODIFIER_2(Border, SetBorder, YGEdge, float);

// Modifier to set this node's width to an absolute number.
LAYOUT_MODIFIER_1(Width, SetWidth, float);

// Modifier to set this node's width to a percentage.
LAYOUT_MODIFIER_1(WidthPercent, SetWidthPercent, float);

// Modifier to set this node's width to auto.
LAYOUT_MODIFIER(WidthAuto, SetWidthAuto, );

// Modifier to set this node's minimum width to an absolute number.
LAYOUT_MODIFIER_1(MinWidth, SetMinWidth, float);

// Modifier to set this node's minimum width to a percentage.
LAYOUT_MODIFIER_1(MinWidthPercent, SetMinWidthPercent, float);

// Modifier to set this node's maximum width to an absolute number.
LAYOUT_MODIFIER_1(MaxWidth, SetMaxWidth, float);

// Modifier to set this node's maximum width to a percentage.
LAYOUT_MODIFIER_1(MaxWidthPercent, SetMaxWidthPercent, float);

// Modifier to set this node's height to an absolute number.
LAYOUT_MODIFIER_1(Height, SetHeight, float);

// Modifier to set this node's height to a percentage.
LAYOUT_MODIFIER_1(HeightPercent, SetHeightPercent, float);

// Modifier to set this node's height to auto.
LAYOUT_MODIFIER(HeightAuto, SetHeightAuto, );

// Modifier to set this node's minimum height to an absolute number.
LAYOUT_MODIFIER_1(MinHeight, SetMinHeight, float);

// Modifier to set this node's minimum height to a percentage.
LAYOUT_MODIFIER_1(MinHeightPercent, SetMinHeightPercent, float);

// Modifier to set this node's maximum height to an absolute number.
LAYOUT_MODIFIER_1(MaxHeight, SetMaxHeight, float);

// Modifier to set this node's maximum height to a percentage.
LAYOUT_MODIFIER_1(MaxHeightPercent, SetMaxHeightPercent, float);

// Modifier to set this node's aspect ratio.
LAYOUT_MODIFIER_1(AspectRatio, SetAspectRatio, float);

// Modifier to set a node's measure function.
NODE_MODIFIER_1(MeasureFunction, SetMeasureFunction,
                std::function<Size(float width, YGMeasureMode width_mode,
                                   float height, YGMeasureMode height_mode)>);
// Modifier to set a node's hit test function.
NODE_MODIFIER_1(HitTestFunction, SetHitTestFunction,
                std::function<bool(const Point& point, const Size& size)>);

// Modifier to add a function to call when a node is drawn.
NODE_MODIFIER_1(OnDraw, OnDraw,
                std::function<void(const DrawContext& context)>);

// Modifier to add a function to call when a node is drawn after the node's
// children have drawn.
NODE_MODIFIER_1(OnDrawPostChildren, OnDrawPostChildren,
                std::function<void(const DrawContext& context)>);

// Modifier to add a function to call when the mouse hovers over the node.
NODE_MODIFIER_1(OnMouseHover, OnMouseHover,
                std::function<void(const Point& point)>);

// Modifier to add a function to call when the mouse leaves the node.
NODE_MODIFIER_1(OnMouseLeave, OnMouseLeave, std::function<void()>);

// Modifier to add a function to call when a mouse button is pressed down while
// over the node.
NODE_MODIFIER_1(OnMouseButtonDown, OnMouseButtonDown,
      std::function<void(const Point& point, window::MouseButton button)>);

// Modifier to add a function to call when a mouse button is released down while
// over the node.
NODE_MODIFIER_1(OnMouseButtonUp, OnMouseButtonUp,
      std::function<void(const Point& point, window::MouseButton button)>);

// Modifier to set whether a node blocks a hit test for hitting nodes
// underneath them.
NODE_MODIFIER_1d(BlocksHitTest, SetBlocksHitTest, bool, true);

// Modifier to add a function to call when the node gains focus.
NODE_MODIFIER_1(OnGainFocus, OnGainFocus, std::function<void()>);

// Modifier to add a function to call when the node loses focus.
NODE_MODIFIER_1(OnLoseFocus, OnLoseFocus, std::function<void()>);

}  // namespace builders
}  // namespace ui
}  // namespace perception
