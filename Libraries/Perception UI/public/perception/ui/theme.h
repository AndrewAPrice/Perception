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

// Default spacing between widgets.
extern uint32 kWidgetSpacing;

// Background color of UI windows.
extern uint32 kBackgroundWindowColor;

// Internal padding for UI windows.
extern float kUiWindowPadding;

// Gap between UI window content and title bar.
extern float kUiWindowTitleBarGap;

// Margin placed around widgets.
extern float kMarginAroundWidgets;

// Default text color for labels.
extern uint32 kLabelTextColor;

// Text color for labels rendered on dark backgrounds.
extern uint32 kLabelOnDarkTextColor;

// Title bar background color when window is focused.
extern uint32 kTitleBarFocusedBackgroundColor;

// Title bar background color when window is unfocused.
extern uint32 kTitleBarUnfocusedBackgroundColor;

// Background color for standard buttons.
extern uint32 kButtonBackgroundColor;

// Background color for buttons on hover.
extern uint32 kButtonBackgroundHoverColor;

// Background color for buttons when pressed.
extern uint32 kButtonBackgroundPushedColor;

// Outline color for buttons.
extern uint32 kButtonOutlineColor;

// Border color for buttons.
extern uint32 kButtonBorderColor;

// Text color for buttons.
extern uint32 kButtonTextColor;

// Border width for buttons.
extern float kButtonBorderWidth;

// Border radius for buttons.
extern float kButtonBorderRadius;

// Internal padding for buttons.
extern float kButtonPadding;

// Minimum width for buttons.
extern float kButtonMinWidth;

// Minimum height for buttons.
extern float kButtonMinHeight;

// Width for image buttons.
extern float kImageButtonWidth;

// Height for image buttons.
extern float kImageButtonHeight;

// Background color for ghost buttons when idle.
extern uint32 kButtonGhostIdleColor;

// Background color for ghost buttons on hover.
extern uint32 kButtonGhostHoverColor;

// Background color for ghost buttons when pressed.
extern uint32 kButtonGhostPushedColor;

// Size (width and height) of checkbox widgets.
extern float kCheckboxSize;

// Size of the check mark inside a checkbox.
extern float kCheckboxMarkerSize;

// Border radius of checkboxes.
extern float kCheckboxBorderRadius;

// Border radius of the check mark marker.
extern float kCheckboxMarkerBorderRadius;

// Border width of checkboxes.
extern float kCheckboxBorderWidth;

// Text color for checkbox labels.
extern uint32 kCheckboxTextColor;

// Spacing between checkbox marker and text label.
extern float kCheckboxSpacing;

// Width of the color picker dialog window.
extern float kColorPickerDialogWidth;

// Height of the color picker dialog window.
extern float kColorPickerDialogHeight;

// Padding inside the color picker dialog.
extern float kColorPickerDialogPadding;

// Gap between elements in the color picker dialog.
extern float kColorPickerDialogGap;

// Background color of the color picker dialog.
extern uint32 kColorPickerDialogBackgroundColor;

// Vertical gap between rows in the color picker dialog.
extern float kColorPickerRowGap;

// Width of labels in the color picker dialog.
extern float kColorPickerLabelWidth;

// Width of value text labels in the color picker dialog.
extern float kColorPickerValueLabelWidth;

// Text color of labels in the color picker dialog.
extern uint32 kColorPickerLabelColor;

// Height of the color preview box.
extern float kColorPickerPreviewHeight;

// Gap around the color preview box.
extern float kColorPickerPreviewGap;

// Border color of the color preview box.
extern uint32 kColorPickerPreviewBorderColor;

// Border width of the color preview box.
extern float kColorPickerPreviewBorderWidth;

// Border radius of the color preview box.
extern float kColorPickerPreviewBorderRadius;

// Width of action buttons in the color picker dialog.
extern float kColorPickerButtonWidth;

// Height of action buttons in the color picker dialog.
extern float kColorPickerButtonHeight;

// Minimum width of combo box widgets.
extern float kComboBoxMinWidth;

// Minimum height of combo box widgets.
extern float kComboBoxMinHeight;

// Left padding of combo box widgets.
extern float kComboBoxPaddingLeft;

// Right padding of combo box widgets.
extern float kComboBoxPaddingRight;

// Border radius of combo box widgets.
extern float kComboBoxBorderRadius;

// Border width of combo box widgets.
extern float kComboBoxBorderWidth;

// Border color of combo box widgets.
extern uint32 kComboBoxBorderColor;

// Background color of text boxes.
extern uint32 kTextBoxBackgroundColor;

// Outline color of text boxes.
extern uint32 kTextBoxOutlineColor;

// Outline color of text boxes on hover.
extern uint32 kTextBoxOutlineHoverColor;

// Outline color of text boxes when focused.
extern uint32 kTextBoxOutlineFocusedColor;

// Text color in text boxes.
extern uint32 kTextBoxTextColor;

// Corner radius of text boxes.
extern float kTextBoxCornerRadius;

// Outline width of text boxes.
extern float kTextBoxOutlineWidth;

// Outline width of text boxes when focused.
extern float kTextBoxOutlineFocusedWidth;

// Internal padding of text boxes.
extern float kTextBoxPadding;

// Width of text insertion cursor in text boxes.
extern float kTextBoxCursorWidth;

// Default width of text boxes.
extern float kTextBoxDefaultWidth;

// Highlight color of selected text in text boxes.
extern uint32 kTextBoxSelectionColor;

// Text color for non-editable text boxes.
extern uint32 kTextBoxNonEditableTextColor;

// Internal padding for text fields.
extern float kTextFieldPadding;

// Width of newline selection indicators in text fields.
extern float kTextFieldNewlineSelectionWidth;

// Minimum width of text selection ranges in text fields.
extern float kTextFieldMinSelectionWidth;

// Minimum width before text wrapping in text fields.
extern float kTextFieldMinWrapWidth;

// Background color of scroll bar thumb handle when idle.
extern uint32 kScrollBarFabBackgroundColor;

// Background color of scroll bar thumb handle on hover.
extern uint32 kScrollBarFabBackgroundHoverColor;

// Outline color of scroll bar thumb handle.
extern uint32 kScrollBarFabOutlineColor;

// Background color of scroll bar track when idle.
extern uint32 kScrollBarTrackBackgroundColor;

// Background color of scroll bar track on hover.
extern uint32 kScrollBarTrackBackgroundHoverColor;

// Outline color of scroll bar track.
extern uint32 kScrollBarTrackOutlineColor;

// Thickness (width/height) of scroll bars.
extern float kScrollBarThickness;

// Border radius of scroll bar thumb handle.
extern float kScrollBarBorderRadius;

// Border radius of scroll containers.
extern float kScrollContainerBorderRadius;

// Outline color of scroll containers.
extern uint32 kScrollContainerOutlineColor;

// Background color of standard containers.
extern uint32 kContainerBackgroundColor;

// Border color of standard containers.
extern uint32 kContainerBorderColor;

// Border width of standard containers.
extern float kContainerBorderWidth;

// Border radius of standard containers.
extern float kContainerBorderRadius;

// Internal padding of standard containers.
extern float kContainerPadding;

// Background color of group box containers.
extern uint32 kGroupBoxBackgroundColor;

// Border color of group box containers.
extern uint32 kGroupBoxBorderColor;

// Border width of group box containers.
extern float kGroupBoxBorderWidth;

// Border radius of group box containers.
extern float kGroupBoxBorderRadius;

// Internal padding of group box containers.
extern float kGroupBoxPadding;

// Title text color of group box containers.
extern uint32 kGroupBoxTitleColor;

// Bottom margin below title in group box containers.
extern float kGroupBoxTitleMarginBottom;

// Right padding of window title bar without action buttons.
extern float kTitleBarRightPaddingWithoutButtons;

// Right padding of window title bar with resizable action buttons.
extern float kTitleBarRightPaddingWithResizableButtons;

// Right padding of window title bar with non-resizable action buttons.
extern float kTitleBarRightPaddingWithNonResizableButtons;

// Negative margin for window title bar.
extern float kTitleBarNegativeMargin;

// Height of tab bar containers.
extern float kTabBarHeight;

// Height of active tabs in tab bar.
extern float kTabBarActiveTabHeight;

// Height of inactive tabs in tab bar.
extern float kTabBarInactiveTabHeight;

// Minimum width of tabs in tab bar.
extern float kTabBarMinTabWidth;

// Maximum width of tabs in tab bar.
extern float kTabBarMaxTabWidth;

// Horizontal padding inside tabs in tab bar.
extern float kTabBarTabHorizontalPadding;

// Corner radius of tabs in tab bar.
extern float kTabBarTabCornerRadius;

// Left margin of close button on tabs in tab bar.
extern float kTabBarCloseButtonMarginLeft;

// Internal padding of tab close buttons.
extern float kTabBarCloseButtonPadding;

// Width of tab close buttons.
extern float kTabBarCloseButtonWidth;

// Thickness of bottom line below tab bar.
extern float kTabBarBottomLineThickness;

// Color of bottom line below tab bar.
extern uint32 kTabBarBottomLineColor;

// Background color of active tab when tab bar is focused.
extern uint32 kTabBarActiveFocusedBackgroundColor;

// Background color of active tab when tab bar is unfocused.
extern uint32 kTabBarActiveUnfocusedBackgroundColor;

// Background color of inactive tabs in tab bar.
extern uint32 kTabBarInactiveBackgroundColor;

// Background color of tabs on hover in tab bar.
extern uint32 kTabBarHoverBackgroundColor;

// Text color of active tabs in tab bar.
extern uint32 kTabBarActiveTextColor;

// Text color of inactive tabs in tab bar.
extern uint32 kTabBarInactiveTextColor;

// Color of close button icon on tabs.
extern uint32 kTabBarCloseButtonColor;

// Color of close button icon on tabs on hover.
extern uint32 kTabBarCloseButtonHoverColor;

// Background color for popup menus.
extern uint32 kPopUpMenuBackgroundColor;

// Border color for popup menus.
extern uint32 kPopUpMenuBorderColor;

// Border width for popup menus.
extern float kPopUpMenuBorderWidth;

// Border radius for popup menus.
extern float kPopUpMenuBorderRadius;

// Padding inside popup menus.
extern float kPopUpMenuPadding;

// Minimum width for popup menus.
extern float kPopUpMenuMinWidth;

// Border width for popup menu items.
extern float kPopUpItemBorderWidth;

// Border radius for popup menu items.
extern float kPopUpItemBorderRadius;

// Idle background color for popup menu items.
extern uint32 kPopUpItemIdleColor;

// Hover background color for popup menu items.
extern uint32 kPopUpItemHoverColor;

// Pushed background color for popup menu items.
extern uint32 kPopUpItemPushedColor;

// Text color for popup menu items.
extern uint32 kPopUpItemTextColor;

// Horizontal padding for popup menu items.
extern float kPopUpItemHorizontalPadding;

// Height for drop down popup menu items.
extern float kPopUpDropDownItemHeight;

// Height for context popup menu items.
extern float kPopUpContextMenuItemHeight;

// Text color for popup category headers.
extern uint32 kPopUpCategoryHeaderTextColor;

// Height for popup category headers.
extern float kPopUpCategoryHeaderHeight;

// Top margin for popup category headers.
extern float kPopUpCategoryHeaderMarginTop;

// Minimum width of slider widgets.
extern float kSliderMinWidth;

// Height of slider widgets.
extern float kSliderHeight;

// Thickness of slider track.
extern float kSliderTrackThickness;

// Radius of slider thumb knob.
extern float kSliderThumbRadius;

// Color of slider track.
extern uint32 kSliderTrackColor;

// Color of slider thumb knob when idle.
extern uint32 kSliderThumbColor;

// Color of slider thumb knob on hover.
extern uint32 kSliderThumbHoverColor;

// Background color of table widgets.
extern uint32 kTableBackgroundColor;

// Border color of table widgets.
extern uint32 kTableBorderColor;

// Border width of table widgets.
extern float kTableBorderWidth;

// Border radius of table widgets.
extern float kTableBorderRadius;

// Background color of table header row.
extern uint32 kTableHeaderBackgroundColor;

// Text color of table header labels.
extern uint32 kTableHeaderTextColor;

// Text color of table header labels on hover.
extern uint32 kTableHeaderHoverTextColor;

// Vertical padding of table header cells.
extern float kTableHeaderVerticalPadding;

// Horizontal padding of table header cells.
extern float kTableHeaderCellHorizontalPadding;

// Color of table row divider lines.
extern uint32 kTableDividerColor;

// Height (thickness) of table row divider lines.
extern float kTableDividerHeight;

// Text color of table body cells.
extern uint32 kTableCellTextColor;

// Background color of highlighted table rows or cells.
extern uint32 kTableCellHighlightColor;

// Transparent color used for unselected table cells.
extern uint32 kTableCellTransparentColor;

// Horizontal padding of table body cells.
extern float kTableCellHorizontalPadding;

// Vertical padding of table body cells.
extern float kTableCellVerticalPadding;

// Background color of tooltip popups.
extern uint32 kTooltipBackgroundColor;

// Border color of tooltip popups.
extern uint32 kTooltipBorderColor;

// Border width of tooltip popups.
extern float kTooltipBorderWidth;

// Border radius of tooltip popups.
extern float kTooltipBorderRadius;

// Internal padding of tooltip popups.
extern float kTooltipPadding;

// Maximum width of tooltip popups before wrapping.
extern float kTooltipMaxWidth;

// Horizontal offset from cursor position for tooltips.
extern float kTooltipOffsetLeft;

// Vertical offset from cursor position for tooltips.
extern float kTooltipOffsetTop;

// Text color of tooltip popups.
extern uint32 kTooltipTextColor;

// Vertical gap between items in tree view.
extern float kTreeViewVerticalGap;

// Indentation width per depth level in tree view.
extern float kTreeViewIndent;

// Width of expand/collapse toggle icon in tree view.
extern float kTreeViewToggleWidth;

// Height of expand/collapse toggle icon in tree view.
extern float kTreeViewToggleHeight;

// Horizontal padding of items in tree view.
extern float kTreeViewItemHorizontalPadding;

// Gap between icon and text in tree view items.
extern float kTreeViewItemGap;

// Border radius of item selection boxes in tree view.
extern float kTreeViewItemBorderRadius;

// Internal padding around tree view containers.
extern float kTreeViewPadding;

// Text color of items in tree view.
extern uint32 kTreeViewItemTextColor;

// Background color of tree view containers.
extern uint32 kTreeViewBackgroundColor;

}  // namespace ui
}  // namespace perception
