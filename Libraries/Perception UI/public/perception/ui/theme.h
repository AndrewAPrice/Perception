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

#include "types.h"

namespace perception {
namespace ui {

extern uint32 kWidgetSpacing;

extern uint32 kBackgroundWindowColor;
extern float kUiWindowPadding;
extern float kUiWindowTitleBarGap;
extern float kMarginAroundWidgets;

extern uint32 kLabelTextColor;
extern uint32 kLabelOnDarkTextColor;

extern uint32 kTitleBarFocusedBackgroundColor;
extern uint32 kTitleBarUnfocusedBackgroundColor;

extern uint32 kButtonBackgroundColor;
extern uint32 kButtonBackgroundHoverColor;
extern uint32 kButtonBackgroundPushedColor;
extern uint32 kButtonOutlineColor;
extern uint32 kButtonBorderColor;
extern uint32 kButtonTextColor;
extern float kButtonBorderWidth;
extern float kButtonBorderRadius;
extern float kButtonPadding;
extern float kButtonMinWidth;
extern float kButtonMinHeight;

extern float kCheckboxSize;
extern float kCheckboxMarkerSize;
extern float kCheckboxBorderRadius;
extern float kCheckboxMarkerBorderRadius;
extern float kCheckboxBorderWidth;
extern uint32 kCheckboxTextColor;
extern float kCheckboxSpacing;

extern float kColorPickerDialogWidth;
extern float kColorPickerDialogHeight;
extern float kColorPickerDialogPadding;
extern float kColorPickerDialogGap;
extern uint32 kColorPickerDialogBackgroundColor;

extern float kColorPickerRowGap;
extern float kColorPickerLabelWidth;
extern float kColorPickerValueLabelWidth;
extern uint32 kColorPickerLabelColor;

extern float kColorPickerPreviewHeight;
extern float kColorPickerPreviewGap;
extern uint32 kColorPickerPreviewBorderColor;
extern float kColorPickerPreviewBorderWidth;
extern float kColorPickerPreviewBorderRadius;

extern float kColorPickerButtonWidth;
extern float kColorPickerButtonHeight;

extern float kComboBoxMinWidth;
extern float kComboBoxMinHeight;
extern float kComboBoxPaddingLeft;
extern float kComboBoxPaddingRight;
extern float kComboBoxBorderRadius;
extern float kComboBoxBorderWidth;
extern uint32 kComboBoxBorderColor;

extern uint32 kTextBoxBackgroundColor;
extern uint32 kTextBoxOutlineColor;
extern uint32 kTextBoxOutlineHoverColor;
extern uint32 kTextBoxOutlineFocusedColor;
extern uint32 kTextBoxTextColor;
extern float kTextBoxCornerRadius;
extern float kTextBoxOutlineWidth;
extern float kTextBoxOutlineFocusedWidth;
extern float kTextBoxPadding;
extern float kTextBoxCursorWidth;
extern float kTextBoxDefaultWidth;
extern uint32 kTextBoxSelectionColor;

extern uint32 kTextBoxNonEditableTextColor;

extern float kTextFieldPadding;
extern float kTextFieldNewlineSelectionWidth;
extern float kTextFieldMinSelectionWidth;
extern float kTextFieldMinWrapWidth;

extern uint32 kScrollBarFabBackgroundColor;
extern uint32 kScrollBarFabBackgroundHoverColor;
extern uint32 kScrollBarFabOutlineColor;
extern uint32 kScrollBarTrackBackgroundColor;
extern uint32 kScrollBarTrackBackgroundHoverColor;
extern uint32 kScrollBarTrackOutlineColor;
extern float kScrollBarThickness;
extern float kScrollBarBorderRadius;
extern float kScrollContainerBorderRadius;

extern uint32 kScrollContainerOutlineColor;

extern uint32 kContainerBackgroundColor;
extern uint32 kContainerBorderColor;
extern float kContainerBorderWidth;
extern float kContainerBorderRadius;
extern float kContainerPadding;

extern uint32 kGroupBoxBackgroundColor;
extern uint32 kGroupBoxBorderColor;
extern float kGroupBoxBorderWidth;
extern float kGroupBoxBorderRadius;
extern float kGroupBoxPadding;
extern uint32 kGroupBoxTitleColor;
extern float kGroupBoxTitleMarginBottom;

extern float kTitleBarRightPaddingWithoutButtons;
extern float kTitleBarRightPaddingWithResizableButtons;
extern float kTitleBarRightPaddingWithNonResizableButtons;
extern float kTitleBarNegativeMargin;

extern float kTabBarHeight;
extern float kTabBarActiveTabHeight;
extern float kTabBarInactiveTabHeight;
extern float kTabBarMinTabWidth;
extern float kTabBarMaxTabWidth;
extern float kTabBarTabHorizontalPadding;
extern float kTabBarTabCornerRadius;
extern float kTabBarCloseButtonMarginLeft;
extern float kTabBarCloseButtonPadding;
extern float kTabBarCloseButtonWidth;
extern float kTabBarBottomLineThickness;

extern uint32 kTabBarBottomLineColor;
extern uint32 kTabBarActiveFocusedBackgroundColor;
extern uint32 kTabBarActiveUnfocusedBackgroundColor;
extern uint32 kTabBarInactiveBackgroundColor;
extern uint32 kTabBarHoverBackgroundColor;
extern uint32 kTabBarActiveTextColor;
extern uint32 kTabBarInactiveTextColor;
extern uint32 kTabBarCloseButtonColor;
extern uint32 kTabBarCloseButtonHoverColor;

extern uint32 kPopUpMenuBackgroundColor;
extern uint32 kPopUpMenuBorderColor;
extern float kPopUpMenuBorderWidth;
extern float kPopUpMenuBorderRadius;
extern float kPopUpMenuPadding;

extern float kSliderMinWidth;
extern float kSliderHeight;
extern float kSliderTrackThickness;
extern float kSliderThumbRadius;
extern uint32 kSliderTrackColor;
extern uint32 kSliderThumbColor;
extern uint32 kSliderThumbHoverColor;

extern uint32 kTableBackgroundColor;
extern uint32 kTableBorderColor;
extern float kTableBorderWidth;
extern float kTableBorderRadius;

extern uint32 kTableHeaderBackgroundColor;
extern uint32 kTableHeaderTextColor;
extern uint32 kTableHeaderHoverTextColor;
extern float kTableHeaderVerticalPadding;
extern float kTableHeaderCellHorizontalPadding;

extern uint32 kTableDividerColor;
extern float kTableDividerHeight;

extern uint32 kTableCellTextColor;
extern uint32 kTableCellHighlightColor;
extern uint32 kTableCellTransparentColor;
extern float kTableCellHorizontalPadding;
extern float kTableCellVerticalPadding;

extern uint32 kTooltipBackgroundColor;
extern uint32 kTooltipBorderColor;
extern float kTooltipBorderWidth;
extern float kTooltipBorderRadius;
extern float kTooltipPadding;
extern float kTooltipMaxWidth;
extern float kTooltipOffsetLeft;
extern float kTooltipOffsetTop;
extern uint32 kTooltipTextColor;

extern float kTreeViewVerticalGap;
extern float kTreeViewIndent;
extern float kTreeViewToggleWidth;
extern float kTreeViewToggleHeight;
extern float kTreeViewItemHorizontalPadding;
extern float kTreeViewItemGap;
extern float kTreeViewItemBorderRadius;
extern float kTreeViewPadding;
extern uint32 kTreeViewItemTextColor;
extern uint32 kTreeViewBackgroundColor;

}  // namespace ui
}  // namespace perception
