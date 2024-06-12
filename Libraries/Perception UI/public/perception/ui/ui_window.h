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

#include <memory>
#include <mutex>
#include <string_view>

#include "include/core/SkRefCnt.h"
#include "include/core/SkSurface.h"
#include "perception/ui/widget.h"
#include "perception/window/window.h"
#include "perception/window/window_delegate.h"

namespace perception {

namespace ui {

struct DrawContext;

class UiWindow : public window::WindowDelegate, public Widget {
 public:
  UiWindow(std::string_view title, bool dialog = false);
  virtual ~UiWindow();

  UiWindow* SetRoot(std::shared_ptr<Widget> root);
  UiWindow* SetBackgroundColor(uint32 background_color);
  std::shared_ptr<Widget> GetRoot();
  UiWindow* OnClose(std::function<void()> on_close_handler);
  UiWindow* OnResize(std::function<void(float, float)> on_resize_handler);

  void Draw();
  UiWindow* Create();

  bool GetWidgetAt(float x, float y, std::shared_ptr<Widget>& widget,
                   float& x_in_selected_widget,
                   float& y_in_selected_widget) override;

  virtual void WindowDraw(const window::WindowDrawBuffer& buffer,
                          window::Rectangle& invalidated_area) override;

  virtual void WindowClosed() override;

  virtual void WindowResized() override;

  virtual void MouseClicked(const window::MouseClickEvent& event) override;

  virtual void MouseLeft() override;

  virtual void MouseHovered(const window::MouseHoverEvent& event) override;

  virtual void InvalidateRender() override;

 private:
  bool invalidated_;
  bool created_;
  bool is_dialog_;

  std::shared_ptr<window::Window> base_window_;

  std::string title_;
  uint32 background_color_;
  std::function<void()> on_close_handler_;
  std::function<void(float, float)> on_resize_handler_;

  std::weak_ptr<Widget> widget_mouse_is_over_;

  void* pixel_data_;
  int buffer_width_;
  int buffer_height_;

  sk_sp<SkSurface> skia_surface_;

  std::mutex window_mutex_;

  void MaybeUpdateLayout();
  void SwitchToMouseOverWidget(std::shared_ptr<Widget> widget);
};

}  // namespace ui
}  // namespace perception
