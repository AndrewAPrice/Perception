// Copyright 2026
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
#include <string>
#include <vector>

#include "inspected_node.h"
#include "perception/ui/image.h"
#include "perception/ui/node.h"
#include "perception/window/base_window.h"

class InspectorPanel;

class UIDebuggerWindow {
 public:
  UIDebuggerWindow(std::shared_ptr<InspectedNode> root,
                   ::perception::window::BaseWindow::Client live_window);
  ~UIDebuggerWindow();

 private:
  std::shared_ptr<::perception::ui::Image> refresh_image_;
  std::shared_ptr<::perception::ui::Image> search_image_;
  std::shared_ptr<InspectedNode> root_node_;
  std::weak_ptr<InspectedNode> selected_node_;
  std::weak_ptr<InspectedNode> hovered_node_;

  bool draw_border_;
  int max_z_depth_;
  float mode_3d_offset_;
  float scale_;
  float pan_x_;
  float pan_y_;

  bool is_panning_;
  bool pan_dragged_;
  ::perception::ui::Point last_mouse_;

  std::string search_query_;

  std::shared_ptr<::perception::ui::Node> canvas_;
  std::shared_ptr<InspectorPanel> inspector_panel_;
  std::shared_ptr<::perception::ui::Node> main_window_;
  std::shared_ptr<::perception::ui::Node> max_z_depth_label_;
  std::shared_ptr<::perception::ui::Node> tree_container_;

  void RebuildTree();

  std::shared_ptr<::perception::ui::Node> BuildTree(
      std::shared_ptr<InspectedNode> node);
  void DrawCanvas(const ::perception::ui::DrawContext& dc);
  void DrawNode(std::shared_ptr<InspectedNode> node,
                const ::perception::ui::DrawContext& dc);
  void DrawMeasurementGuides(const ::perception::ui::DrawContext& dc);
  std::shared_ptr<InspectedNode> HitTestNode(
      std::shared_ptr<InspectedNode> node, ::perception::ui::Point pt);
  void UpdateInspector();

  bool is_live_;
  ::perception::window::BaseWindow::Client live_window_;
  ::perception::MessageId disappearance_message_id_;
  std::shared_ptr<::perception::ui::Node> live_controls_;

  void LeaveLiveMode();
  void TweakNodeProperty(std::string_view node_id,
                         std::string_view property_name,
                         std::string_view property_value);

  uint64 next_temp_id_;

  void ShowTreeContextMenu(std::shared_ptr<InspectedNode> node,
                           ::perception::ui::Point abs_pt);
  void CenterNodeInView(std::shared_ptr<InspectedNode> node);
  void CreateChildNode(std::shared_ptr<InspectedNode> parent_node);
  void DeleteInspectedNode(std::shared_ptr<InspectedNode> node);
  void ReparentInspectedNode(std::shared_ptr<InspectedNode> node,
                             std::shared_ptr<InspectedNode> new_parent);
  std::shared_ptr<InspectedNode> FindNodeById(
      std::shared_ptr<InspectedNode> node, std::string_view id);
  bool IsDescendantOf(std::shared_ptr<InspectedNode> node,
                      std::shared_ptr<InspectedNode> ancestor);
  void RefreshHierarchyAndSelect(std::string_view select_id);
};
