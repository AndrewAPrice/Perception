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

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "perception/ui/container.h"
#include "perception/ui/label.h"
#include "perception/ui/parentless_widget.h"
#include "perception/ui/scroll_bar.h"
#include "perception/ui/widget.h"

namespace perception {
namespace ui {

struct DrawContext;

class ScrollContainer : public Container {
 public:
  // Creates a standard button with a text label.
  static std::shared_ptr<ScrollContainer> Create(
      std::shared_ptr<ParentlessWidget> content_, bool show_vertical_scroll_bar,
      bool show_horizontal_scroll_bar);

  virtual ~ScrollContainer();

  virtual bool GetWidgetAt(float x, float y, std::shared_ptr<Widget>& widget,
                           float& x_in_selected_widget,
                           float& y_in_selected_widget) override;

  ScrollContainer* ScrollTo(float x, float y);
  double GetScrollX() const;
  double GetScrollY() const;

  ScrollContainer* ShowVerticalScrollBar(bool show);
  ScrollContainer* ShowHorizontalScrollBar(bool show);
  bool IsShowingHorizontalScrollBar() const;
  bool IsShowingVerticalScrollBar() const;

  std::shared_ptr<Widget> GetInnerContainer();

 private:
  ScrollContainer(std::shared_ptr<ParentlessWidget> content_,
                  bool show_vertical_scroll_bar,
                  bool show_horizontal_scroll_bar);
  virtual void Draw(DrawContext& draw_context) override;
  virtual void DrawContents(DrawContext& draw_context) override;
  void RebuildLayout();
  static void LayoutDirtied(YGNode* node);
  void InvalidateScrollBars();
  void MaybeUpdateScrollBars();
  void ScrollToInternal(float x, float y);
  void MaybeUpdateContentSize();
  void CheckIfContentsChangedSize();

  std::shared_ptr<ParentlessWidget> content_;
  std::shared_ptr<Widget> inner_container_;
  std::shared_ptr<ScrollBar> vertical_scroll_bar_;
  std::shared_ptr<ScrollBar> horizontal_scroll_bar_;

  double scroll_x_;
  double scroll_y_;
  bool show_vertical_scroll_bar_;
  bool show_horizontal_scroll_bar_;
  bool scroll_bars_invalidated_;
  double last_calculated_width_;
  double last_calculated_height_;
  double last_content_width_;
  double last_content_height_;
};

}  // namespace ui
}  // namespace perception
