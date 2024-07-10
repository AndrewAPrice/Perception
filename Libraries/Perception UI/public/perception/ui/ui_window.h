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

#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string_view>

#include "include/core/SkRefCnt.h"
#include "include/core/SkSurface.h"
#include "perception/ui/node.h"
#include "perception/window/window.h"
#include "perception/window/window_delegate.h"

namespace perception {

namespace ui {

struct DrawContext;
struct Point;

class UiWindow : public window::WindowDelegate,
                 public std::enable_shared_from_this<UiWindow> {
 public:
  UiWindow();
  virtual ~UiWindow();

  void SetNode(std::weak_ptr<Node> node);
  void SetBackgroundColor(uint32 background_color);
  void OnClose(std::function<void()> on_close_handler);
  void SetTitle(std::string_view title);
  void SetIsDialog(bool is_dialog);
  void OnResize(std::function<void()> on_resize_handler);
  void FocusOnNode();

  void Draw();
  void GetNodesAt(
      const Point& point,
      const std::function<void(Node& node, const Point& point_in_node)>&
          on_hit_node);

  virtual void WindowDraw(const window::WindowDrawBuffer& buffer,
                          window::Rectangle& invalidated_area) override;

  virtual void WindowClosed() override;

  virtual void WindowResized() override;

  virtual void MouseClicked(const window::MouseClickEvent& event) override;

  virtual void MouseLeft() override;

  virtual void MouseHovered(const window::MouseHoverEvent& event) override;

  void InvalidateRender();

 private:
  struct NodeWeakPtrComparator {
    bool operator()(const std::weak_ptr<Node>& a,
                    const std::weak_ptr<Node>& b) const {
      if (a.expired() || b.expired()) return b.owner_before(a);

      return a.lock() < b.lock();
    }
  };

  bool invalidated_;
  bool created_;
  bool is_dialog_;

  void Create();

  std::shared_ptr<window::Window> base_window_;
  std::weak_ptr<Node> node_;

  std::string title_;
  uint32 background_color_;
  std::vector<std::function<void()>> on_close_functions_;
  std::vector<std::function<void()>> on_resize_functions_;

  std::weak_ptr<Node> node_mouse_is_over_;

  void* pixel_data_;
  int buffer_width_;
  int buffer_height_;

  sk_sp<SkSurface> skia_surface_;

  std::mutex window_mutex_;

  std::set<std::weak_ptr<Node>, NodeWeakPtrComparator>
      nodes_to_notify_when_mouse_leaves_;

  void HandleMouseEvent(
      const Point& point,
      const std::function<void(Node& node, const Point& point_in_node)>&
          on_each_node);
};

}  // namespace ui
}  // namespace perception
