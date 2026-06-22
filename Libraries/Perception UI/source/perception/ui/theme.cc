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

#include "perception/ui/theme.h"

#include "include/core/SkColor.h"

namespace perception {
namespace ui {

inline uint32 HSVColor(float h, float s, float v) {
  float colors[3] = {h, s, v};
  return SkHSVToColor(colors);
}

uint32 kWidgetSpacing = 8.0f;

uint32 kBackgroundWindowColor = HSVColor(0.0f, 0.0f, 0.95f);
float kUiWindowPadding = 8.0f;
float kUiWindowTitleBarGap = 8.0f;
float kMarginAroundWidgets = 4.0f;

uint32 kLabelTextColor = SkColorSetARGB(0xff, 0x0, 0x0, 0x0);
uint32 kLabelOnDarkTextColor = SkColorSetARGB(0xff, 0xff, 0xff, 0xff);

uint32 kTitleBarFocusedBackgroundColor = SkColorSetARGB(0xff, 0x7e, 0xa0, 0xf8);
uint32 kTitleBarUnfocusedBackgroundColor =
    SkColorSetARGB(0xff, 0xd9, 0xd9, 0xd9);

uint32 kButtonBackgroundColor = HSVColor(0.0f, 0.0f, 1.0f);
uint32 kButtonBackgroundHoverColor = HSVColor(0.0f, 0.0f, 0.925f);
uint32 kButtonBackgroundPushedColor = HSVColor(0.0f, 0.0f, 0.9f);
uint32 kButtonOutlineColor = HSVColor(0.0f, 0.0f, 0.80f);
uint32 kButtonBorderColor = 0xFF000000;
uint32 kButtonTextColor = kLabelTextColor;
float kButtonBorderWidth = 1.0f;
float kButtonBorderRadius = 4.0f;
float kButtonPadding = 8.0f;
float kButtonMinWidth = 24.0f;
float kButtonMinHeight = 32.0f;

float kCheckboxSize = 16.0f;
float kCheckboxMarkerSize = 8.0f;
float kCheckboxBorderRadius = 4.0f;
float kCheckboxMarkerBorderRadius = 2.0f;
float kCheckboxBorderWidth = 1.0f;
uint32 kCheckboxTextColor = 0xFF1F2937;
float kCheckboxSpacing = 8.0f;

float kColorPickerDialogWidth = 300.0f;
float kColorPickerDialogHeight = 420.0f;
float kColorPickerDialogPadding = 12.0f;
float kColorPickerDialogGap = 12.0f;
uint32 kColorPickerDialogBackgroundColor = 0xFFF3F4F6;

float kColorPickerRowGap = 8.0f;
float kColorPickerLabelWidth = 20.0f;
float kColorPickerValueLabelWidth = 40.0f;
uint32 kColorPickerLabelColor = 0xFF374151;

float kColorPickerPreviewHeight = 60.0f;
float kColorPickerPreviewGap = 8.0f;
uint32 kColorPickerPreviewBorderColor = 0xFFD1D5DB;
float kColorPickerPreviewBorderWidth = 1.0f;
float kColorPickerPreviewBorderRadius = 8.0f;

float kColorPickerButtonWidth = 70.0f;
float kColorPickerButtonHeight = 32.0f;

float kComboBoxMinWidth = 80.0f;
float kComboBoxMinHeight = 24.0f;
float kComboBoxPaddingLeft = 8.0f;
float kComboBoxPaddingRight = 24.0f;
float kComboBoxBorderRadius = 4.0f;
float kComboBoxBorderWidth = 1.0f;
uint32 kComboBoxBorderColor = kButtonOutlineColor;

uint32 kTextBoxBackgroundColor = kButtonBackgroundColor;
uint32 kTextBoxOutlineColor = kButtonOutlineColor;
uint32 kTextBoxOutlineHoverColor = 0xFF9CA3AF;
uint32 kTextBoxOutlineFocusedColor = 0xFF3B82F6;
uint32 kTextBoxTextColor = kLabelTextColor;
float kTextBoxCornerRadius = kButtonBorderRadius;
float kTextBoxOutlineWidth = 1.0f;
float kTextBoxOutlineFocusedWidth = 2.0f;
float kTextBoxPadding = 8.0f;
float kTextBoxCursorWidth = 1.5f;
float kTextBoxDefaultWidth = 150.0f;
uint32 kTextBoxSelectionColor = 0xFFBFDBFE;

uint32 kTextBoxNonEditableTextColor = SkColorSetARGB(0xff, 0x11, 0x11, 0x11);

float kTextFieldPadding = 2.0f;
float kTextFieldNewlineSelectionWidth = 6.0f;
float kTextFieldMinSelectionWidth = 4.0f;
float kTextFieldMinWrapWidth = 20.0f;

uint32 kScrollBarFabBackgroundColor = HSVColor(0.0f, 0.0f, 0.9f);
uint32 kScrollBarFabBackgroundHoverColor = HSVColor(0.0f, 0.0f, 0.825f);
uint32 kScrollBarFabOutlineColor = HSVColor(0.0f, 0.0f, 0.8f);
uint32 kScrollBarTrackBackgroundColor = HSVColor(0.0f, 0.0f, 1.0f);
uint32 kScrollBarTrackBackgroundHoverColor = HSVColor(0.0f, 0.0f, 0.975f);
uint32 kScrollBarTrackOutlineColor = HSVColor(0.0f, 0.0f, 0.8f);
float kScrollBarThickness = 16.0f;
float kScrollBarBorderRadius = kScrollBarThickness / 2.0f;
float kScrollContainerBorderRadius = kScrollBarBorderRadius;

uint32 kScrollContainerOutlineColor = kScrollBarTrackOutlineColor;

uint32 kContainerBackgroundColor = HSVColor(0.0f, 0.0f, 0.97f);
uint32 kContainerBorderColor = kButtonOutlineColor;
float kContainerBorderWidth = 1.0f;
float kContainerBorderRadius = kButtonBorderRadius;
float kContainerPadding = kMarginAroundWidgets;

uint32 kGroupBoxBackgroundColor = SkColorSetARGB(0xff, 0xf5, 0xf5, 0xf5);
uint32 kGroupBoxBorderColor = SkColorSetARGB(0xff, 0xe0, 0xe0, 0xe0);
float kGroupBoxBorderWidth = 1.0f;
float kGroupBoxBorderRadius = 4.0f;
float kGroupBoxPadding = 8.0f;
uint32 kGroupBoxTitleColor = SkColorSetARGB(0xff, 0x33, 0x33, 0x33);
float kGroupBoxTitleMarginBottom = 4.0f;

float kTitleBarRightPaddingWithoutButtons = 6.0f;
float kTitleBarRightPaddingWithResizableButtons = 60.0f;
float kTitleBarRightPaddingWithNonResizableButtons = 42.0f;
float kTitleBarNegativeMargin = -8.0f;

float kTabBarHeight = 30.0f;
float kTabBarActiveTabHeight = 26.0f;
float kTabBarInactiveTabHeight = 24.0f;
float kTabBarMinTabWidth = 50.0f;
float kTabBarMaxTabWidth = 150.0f;
float kTabBarTabHorizontalPadding = 8.0f;
float kTabBarTabCornerRadius = 4.0f;
float kTabBarCloseButtonMarginLeft = 6.0f;
float kTabBarCloseButtonPadding = 2.0f;
float kTabBarCloseButtonWidth = 24.0f;
float kTabBarBottomLineThickness = 1.0f;

uint32 kTabBarBottomLineColor = SkColorSetARGB(0xFF, 0xD0, 0xD0, 0xD0);
uint32 kTabBarActiveFocusedBackgroundColor = 0xFFF8F9FA;
uint32 kTabBarActiveUnfocusedBackgroundColor = 0xFFE9ECEF;
uint32 kTabBarInactiveBackgroundColor = 0xFFCED4DA;
uint32 kTabBarHoverBackgroundColor = 0xFFDEE2E6;
uint32 kTabBarActiveTextColor = 0xFF000000;
uint32 kTabBarInactiveTextColor = 0xFF495057;
uint32 kTabBarCloseButtonColor = 0xFF6C757D;
uint32 kTabBarCloseButtonHoverColor = 0xFFDC3545;

uint32 kPopUpMenuBackgroundColor = 0xFFFFFFFF;
uint32 kPopUpMenuBorderColor = kButtonOutlineColor;
float kPopUpMenuBorderWidth = 1.0f;
float kPopUpMenuBorderRadius = 4.0f;
float kPopUpMenuPadding = 2.0f;

float kSliderMinWidth = 100.0f;
float kSliderHeight = 16.0f;
float kSliderTrackThickness = 4.0f;
float kSliderThumbRadius = 8.0f;
uint32 kSliderTrackColor = 0xFFD1D5DB;
uint32 kSliderThumbColor = 0xFF4F46E5;
uint32 kSliderThumbHoverColor = 0xFF4338CA;

uint32 kTableBackgroundColor = 0xFFFFFFFF;
uint32 kTableBorderColor = 0xFFCCCCCC;
float kTableBorderWidth = 1.0f;
float kTableBorderRadius = 4.0f;

uint32 kTableHeaderBackgroundColor = 0xFFEAEAEA;
uint32 kTableHeaderTextColor = 0xFF000000;
uint32 kTableHeaderHoverTextColor = 0xFF555555;
float kTableHeaderVerticalPadding = 6.0f;
float kTableHeaderCellHorizontalPadding = 8.0f;

uint32 kTableDividerColor = 0xFFCCCCCC;
float kTableDividerHeight = 1.0f;

uint32 kTableCellTextColor = 0xFF000000;
uint32 kTableCellHighlightColor = 0xFFF0F0F0;
uint32 kTableCellTransparentColor = 0x00000001;
float kTableCellHorizontalPadding = 8.0f;
float kTableCellVerticalPadding = 4.0f;

uint32 kTooltipBackgroundColor = 0xE0202020;
uint32 kTooltipBorderColor = 0xFF000000;
float kTooltipBorderWidth = 1.0f;
float kTooltipBorderRadius = 2.0f;
float kTooltipPadding = 4.0f;
float kTooltipMaxWidth = 200.0f;
float kTooltipOffsetLeft = 8.0f;
float kTooltipOffsetTop = 12.0f;
uint32 kTooltipTextColor = kLabelOnDarkTextColor;

float kTreeViewVerticalGap = 2.0f;
float kTreeViewIndent = 16.0f;
float kTreeViewToggleWidth = 16.0f;
float kTreeViewToggleHeight = 20.0f;
float kTreeViewItemHorizontalPadding = 4.0f;
float kTreeViewItemGap = 4.0f;
float kTreeViewItemBorderRadius = 4.0f;
float kTreeViewPadding = 8.0f;
uint32 kTreeViewItemTextColor = 0xFF111827;
uint32 kTreeViewBackgroundColor = 0xFFFFFFFF;

}  // namespace ui
}  // namespace perception