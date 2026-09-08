// Copyright 2026 Google LLC
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

#include "include/core/SkColor.h"
#include "perception/type_id.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/node.h"
#include "perception/ui/theme.h"

namespace perception {
namespace ui {
namespace components {

class Tooltip;

// Segment details for rendering in the segmented bar widget.
struct SegmentedBarSegment {
  // Label for this segment (e.g. "Partition 1", "Used", "Free").
  std::string label;

  // Proportion of the bar (0.0 to 1.0).
  float ratio = 0.0f;

  // Color of this segment.
  SkColor color = SK_ColorGRAY;
};

// Custom visual widget rendering segmented breakdown with hover tooltips.
class SegmentedBar : public UniqueIdentifiableType<SegmentedBar>,
                     public std::enable_shared_from_this<SegmentedBar> {
 public:
  // Creates a new Node with attached segmented bar rendering.
  template <typename... Modifiers>
  static std::shared_ptr<Node> BasicSegmentedBar(
      std::shared_ptr<SegmentedBar>& out_controller, Modifiers... modifiers) {
    return Node::Empty(
        [](Layout& layout) {
          layout.SetWidthPercent(100.0f);
          layout.SetHeight(kSegmentedBarHeight);
          layout.SetFlexShrink(0.0f);
        },
        &out_controller,
        modifiers...);
  }

  // Creates a new Node with attached segmented bar rendering.
  template <typename... Modifiers>
  static std::shared_ptr<Node> BasicSegmentedBar(Modifiers... modifiers) {
    return Node::Empty(
        [](Layout& layout) {
          layout.SetWidthPercent(100.0f);
          layout.SetHeight(kSegmentedBarHeight);
          layout.SetFlexShrink(0.0f);
        },
        [](SegmentedBar& bar) {},
        modifiers...);
  }

  // Backwards compatibility aliases for BasicStorageBar.
  template <typename... Modifiers>
  static std::shared_ptr<Node> BasicStorageBar(
      std::shared_ptr<SegmentedBar>& out_controller, Modifiers... modifiers) {
    return BasicSegmentedBar(out_controller, modifiers...);
  }

  template <typename... Modifiers>
  static std::shared_ptr<Node> BasicStorageBar(Modifiers... modifiers) {
    return BasicSegmentedBar(modifiers...);
  }

  SegmentedBar();
  virtual ~SegmentedBar();

  // Sets the list of segments to display.
  void SetSegments(const std::vector<SegmentedBarSegment>& segments);

  // Draws the segmented bar onto the canvas.
  void Draw(const DrawContext& context);

  // Sets the weak reference to the node and registers draw and hover listeners.
  void SetNode(std::weak_ptr<Node> node);

 private:
  // Finds the index of the segment at the given horizontal offset, or -1 if none.
  int GetSegmentIndexAt(float x, float width) const;

  // Handles mouse hover events over the bar.
  void HandleMouseHover(const Point& point);

  // Handles mouse leave events.
  void HandleMouseLeave();

  // Formats the tooltip text for a segment.
  std::string FormatTooltipText(const SegmentedBarSegment& segment) const;

  // List of active segments.
  std::vector<SegmentedBarSegment> segments_;

  // Weak reference to the host UI node.
  std::weak_ptr<Node> node_;

  // Shared pointer to tooltip component on the host node.
  std::shared_ptr<Tooltip> tooltip_;

  // Index of currently hovered segment, or -1 if none.
  int last_hovered_segment_index_ = -1;
};

// Backwards compatibility aliases.
using StorageBar = SegmentedBar;
using StorageBarSegment = SegmentedBarSegment;

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::SegmentedBar>;

}  // namespace perception
