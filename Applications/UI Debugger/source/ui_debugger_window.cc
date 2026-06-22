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

#include "ui_debugger_window.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkString.h"
#include "inspector_panel.h"
#include "inspector_panel_input_components.h"
#include "perception/processes.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/checkbox.h"
#include "perception/ui/components/color_picker_dialog.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/image_view.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/pop_up.h"
#include "perception/ui/components/resizable_container.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/slider.h"
#include "perception/ui/components/tree_view.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/font.h"
#include "perception/ui/image.h"
#include "perception/ui/layout.h"

using ::perception::TerminateProcess;
using ::perception::ui::DrawContext;
using ::perception::ui::GetBold12UiFont;
using ::perception::ui::Image;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Checkbox;
using ::perception::ui::components::Container;
using ::perception::ui::components::ImageView;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::PopUp;
using ::perception::ui::components::PopUpMenu;
using ::perception::ui::components::ResizableContainer;
using ::perception::ui::components::ResizableContainerItem;
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::ShowColorPickerDialog;
using ::perception::ui::components::Slider;
using ::perception::ui::components::TreeView;
using ::perception::ui::components::TreeViewItem;
using ::perception::ui::components::UiWindow;
using ::perception::window::MouseButton;

namespace {

std::shared_ptr<InspectedNode> FindNodeByTreeItem(
    std::shared_ptr<InspectedNode> node,
    std::shared_ptr<::perception::ui::components::TreeViewItem> item) {
  if (!node || !item) return nullptr;
  if (node->tree_item.lock() == item) return node;
  for (auto& child : node->children) {
    if (auto res = FindNodeByTreeItem(child, item)) return res;
  }
  return nullptr;
}

bool CaseInsensitiveContains(std::string_view str, std::string_view query) {
  if (query.empty()) return true;
  if (str.size() < query.size()) return false;
  auto it = std::search(str.begin(), str.end(), query.begin(), query.end(),
                        [](char a, char b) {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                        });
  return it != str.end();
}

bool MatchesQuery(std::shared_ptr<InspectedNode> node, std::string_view query) {
  if (!node || query.empty()) return false;
  if (query[0] == '#' && query.size() > 1) {
    return CaseInsensitiveContains(node->id, query.substr(1));
  }
  return CaseInsensitiveContains(node->name, query) ||
         CaseInsensitiveContains(node->id, query);
}

bool HasMatchingDescendant(std::shared_ptr<InspectedNode> node,
                           std::string_view query) {
  if (!node || query.empty()) return false;
  for (auto& child : node->children) {
    if (MatchesQuery(child, query) || HasMatchingDescendant(child, query)) {
      return true;
    }
  }
  return false;
}

SkRect GetNodeScreenRect(std::shared_ptr<InspectedNode> node, float canvas_x,
                         float canvas_y, float pan_x, float pan_y,
                         float mode_3d_offset, float scale) {
  float rx = canvas_x + pan_x +
             (node->global_position.x + mode_3d_offset * node->z_depth) * scale;
  float ry = canvas_y + pan_y +
             (node->global_position.y + mode_3d_offset * node->z_depth) * scale;
  float rw = node->size.width * scale;
  float rh = node->size.height * scale;
  return SkRect::MakeXYWH(rx, ry, rw, rh);
}

void DrawDimensionLineAndBadge(SkCanvas* canvas, Point p1, Point p2,
                               float app_dist) {
  if (app_dist < 1.0f) return;

  SkPaint lp;
  lp.setColor(0xFFE11D48);  // Crimson
  lp.setStrokeWidth(1.5f);
  canvas->drawLine(p1.x, p1.y, p2.x, p2.y, lp);

  std::string text =
      std::to_string(static_cast<int>(std::round(app_dist))) + "px";
  SkFont* font = ::perception::ui::GetBold12UiFont();
  SkRect text_bounds;
  font->measureText(text.c_str(), text.size(), SkTextEncoding::kUTF8,
                    &text_bounds);

  float badge_w = text_bounds.width() + 12.0f;
  float badge_h = text_bounds.height() + 8.0f;

  float mid_x = (p1.x + p2.x) / 2.0f;
  float mid_y = (p1.y + p2.y) / 2.0f;

  SkRect badge_rect = SkRect::MakeXYWH(
      mid_x - badge_w / 2.0f, mid_y - badge_h / 2.0f, badge_w, badge_h);

  SkPaint bg_paint;
  bg_paint.setColor(0xFFE11D48);
  bg_paint.setAntiAlias(true);
  canvas->drawRoundRect(badge_rect, 4.0f, 4.0f, bg_paint);

  SkPaint text_paint;
  text_paint.setColor(0xFFFFFFFF);
  text_paint.setAntiAlias(true);
  canvas->drawString(
      SkString(text.c_str(), text.size()), mid_x - text_bounds.width() / 2.0f,
      mid_y + text_bounds.height() / 2.0f - 1.0f, *font, text_paint);
}

}  // namespace

UIDebuggerWindow::UIDebuggerWindow(
    std::shared_ptr<InspectedNode> root,
    ::perception::window::BaseWindow::Client live_window)
    : root_node_(root),
      selected_node_(),
      hovered_node_(),
      draw_border_(true),
      max_z_depth_(100),
      mode_3d_offset_(0.0f),
      scale_(1.0f),
      pan_x_(0.0f),
      pan_y_(0.0f),
      is_panning_(false),
      pan_dragged_(false),
      last_mouse_({0, 0}),
      search_query_(""),
      is_live_(live_window.IsValid()),
      disappearance_message_id_(0),
      next_temp_id_(1000000) {
  refresh_image_ = Image::LoadImage("/Applications/UI Debugger/refresh.png");
  search_image_ = Image::LoadImage("/Applications/UI Debugger/search.png");
  if (is_live_) {
    live_window_ = live_window;
    disappearance_message_id_ =
        live_window_.NotifyOnDisappearance([this]() { LeaveLiveMode(); });
    live_controls_ = Container::HorizontalContainer(
        [](Layout& l) {
          l.SetAlignItems(YGAlignCenter);
          l.SetGap(8.0f);
        },
        // Green pill that says "Live"
        Container::HorizontalContainer(
            [](Layout& l) {
              l.SetPadding(YGEdgeHorizontal, 12.0f);
              l.SetPadding(YGEdgeVertical, 4.0f);
              l.SetAlignItems(YGAlignCenter);
              l.SetJustifyContent(YGJustifyCenter);
            },
            [](Block& b) {
              b.SetFillColor(0xFF10B981);
              b.SetBorderRadius(12.0f);
            },
            Label::BasicLabel("Live",
                              [](Label& l) {
                                l.SetFont(GetBold12UiFont());
                                l.SetColor(0xFFFFFFFF);
                              })),
        // Borderless/backgroundless button showing a refresh symbol
        Button::BasicButton(
            [this]() {
              auto hierarchy = live_window_.GetUiHierarchy();
              if (hierarchy) {
                try {
                  ::nlohmann::json data =
                      ::nlohmann::json::parse(hierarchy->json);
                  root_node_ =
                      ParseInspectedNode(data, 0, ::perception::ui::Point{0, 0},
                                         ::perception::ui::Point{0, 0});
                  selected_node_ = {};
                  RebuildTree();
                  UpdateInspector();
                  canvas_->Invalidate();
                } catch (...) {
                }
              }
            },
            [](Layout& l) {
              l.SetWidth(32.0f);
              l.SetHeight(32.0f);
              l.SetMinWidth(32.0f);
              l.SetMinHeight(32.0f);
              l.SetAlignItems(YGAlignCenter);
              l.SetJustifyContent(YGJustifyCenter);
              l.SetPadding(YGEdgeAll, 0.0f);
            },
            [](Block& b) {
              b.SetBorderWidth(0.0f);
              b.SetBorderRadius(16.0f);
            },
            [](Button& btn) {
              btn.SetIdleColor(0x00000000);
              btn.SetHoverColor(0xFFE5E7EB);
              btn.SetPushedColor(0xFFD1D5DB);
            },
            refresh_image_
                ? ImageView::BasicImage(
                      refresh_image_,
                      [](Layout& l) {
                        l.SetWidth(18.0f);
                        l.SetHeight(18.0f);
                      },
                      [](ImageView& iv) {
                        iv.SetResizeMethod(
                            ::perception::ui::ResizeMethod::Contain);
                        iv.SetAlignment(
                            ::perception::ui::TextAlignment::MiddleCenter);
                      })
                : Label::BasicLabel(
                      "🔄", [](Label& l) { l.SetFont(GetBold12UiFont()); })));
  } else {
    live_controls_ = Node::Empty();
  }
  max_z_depth_ = GetMaxDepth(root_node_);

  max_z_depth_label_ = Label::BasicLabel(std::to_string(max_z_depth_));

  inspector_panel_ = std::make_shared<InspectorPanel>(
      [this](std::string_view prop_name, std::string_view prop_val) {
        if (auto node = selected_node_.lock()) {
          TweakNodeProperty(node->id, prop_name, prop_val);
        }
      });

  UpdateInspector();

  tree_container_ = TreeView::Create();
  if (auto tv = tree_container_->Get<TreeView>()) {
    tv->OnDrop([this](std::shared_ptr<TreeViewItem> source_item,
                      std::shared_ptr<TreeViewItem> target_item) {
      auto source_node = FindNodeByTreeItem(root_node_, source_item);
      auto target_node = FindNodeByTreeItem(root_node_, target_item);
      if (source_node && target_node && source_node != target_node &&
          !IsDescendantOf(target_node, source_node)) {
        ReparentInspectedNode(source_node, target_node);
      }
    });
  }
  RebuildTree();

  auto search_box = InputBox::BasicInputBox(
      "", [](Layout& layout) { layout.SetFlexGrow(1.0f); },
      [this](InputBox& ib) {
        ib.OnTextChanged([this](std::string_view text) {
          search_query_ = text;
          RebuildTree();
          canvas_->Invalidate();
        });
      });

  auto search_container = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetPadding(YGEdgeAll, 8.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetBorder(YGEdgeBottom, 1.0f);
      },
      [](Block& block) {
        block.SetFillColor(0xFFF9FAFB);
        block.SetBorderColor(0xFFE5E7EB);
      },
      search_image_
          ? ImageView::BasicImage(
                search_image_,
                [](Layout& l) {
                  l.SetWidth(16.0f);
                  l.SetHeight(16.0f);
                  l.SetMargin(YGEdgeRight, 6.0f);
                },
                [](ImageView& iv) {
                  iv.SetResizeMethod(::perception::ui::ResizeMethod::Contain);
                  iv.SetAlignment(
                      ::perception::ui::TextAlignment::MiddleCenter);
                })
          : Label::BasicLabel(
                "🔍", [](Layout& l) { l.SetMargin(YGEdgeRight, 6.0f); }),
      search_box);

  auto tree_panel = Container::VerticalContainer(
      [](ResizableContainerItem& item) {
        item.SetBehavior(ResizableContainerItem::Behavior::Fixed);
      },
      [](Layout& layout) {
        layout.SetWidth(240.0f);
        layout.SetHeightPercent(100.0f);
        layout.SetMinHeight(0.0f);
      },
      [](Block& block) {
        block.SetBorderColor(0xFFE5E7EB);
        block.SetBorderWidth(1.0f);
      },
      search_container, tree_container_);

  canvas_ = Container::VerticalContainer(
      [](ResizableContainerItem& item) {
        item.SetBehavior(ResizableContainerItem::Behavior::Flex);
      },
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetMinHeight(0.0f);
        layout.SetHeightPercent(100.0f);
      },
      [](Block& block) { block.SetFillColor(0xFFFFFFFF); });

  canvas_->OnDraw([this](const DrawContext& dc) { DrawCanvas(dc); });

  canvas_->OnMouseButtonDown([this](const Point& pt, MouseButton button) {
    if (button == MouseButton::Right) {
      auto hit = HitTestNode(root_node_, pt);
      if (hit) {
        selected_node_ = hit;
        UpdateInspector();
        canvas_->Invalidate();
        if (auto item = hit->tree_item.lock()) {
          item->Select(true, false);
        }
        ShowTreeContextMenu(hit, canvas_->GetAbsolutePosition() + pt);
      }
      return;
    }
    if (button == MouseButton::Left) {
      is_panning_ = true;
      last_mouse_ = pt;
      pan_dragged_ = false;
    }
  });

  canvas_->OnMouseHover([this](const Point& pt) {
    if (is_panning_) {
      float dx = pt.x - last_mouse_.x;
      float dy = pt.y - last_mouse_.y;
      if (std::abs(dx) > 2.0f || std::abs(dy) > 2.0f) pan_dragged_ = true;
      pan_x_ += dx;
      pan_y_ += dy;
      last_mouse_ = pt;
      canvas_->Invalidate();
    } else {
      auto hover_hit = HitTestNode(root_node_, pt);
      if (hover_hit != hovered_node_.lock()) {
        hovered_node_ = hover_hit;
        canvas_->Invalidate();
      }
    }
  });

  canvas_->OnMouseButtonUp([this](const Point& pt, MouseButton) {
    if (is_panning_) {
      is_panning_ = false;
      if (!pan_dragged_) {
        auto hit = HitTestNode(root_node_, pt);
        selected_node_ = hit;
        UpdateInspector();
        canvas_->Invalidate();
        if (hit) {
          if (auto item = hit->tree_item.lock()) {
            item->Select(true, false);
          }
        }
      }
    }
  });

  std::string window_title = "UI Debugger";
  if (root_node_ && !root_node_->window_title.empty())
    window_title += " - " + root_node_->window_title;

  main_window_ = UiWindow::ResizableWindowWithTitleBar(
      window_title,
      [](Layout& layout) {
        layout.SetWidth(800.0f);
        layout.SetHeight(600.0f);
      },
      [](UiWindow& window) { window.OnClose([]() { TerminateProcess(); }); },
      // Toolbar
      Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetPadding(YGEdgeRight, 16.0f);
            layout.SetAlignItems(YGAlignCenter);
            layout.SetGap(16.0f);
          },
          live_controls_,
          Checkbox::BasicCheckbox("Draw Border", true,
                                  [this](bool checked) {
                                    draw_border_ = checked;
                                    canvas_->Invalidate();
                                  }),
          // Max Z-Depth
          Container::HorizontalContainer(
              [](Layout& l) {
                l.SetAlignItems(YGAlignCenter);
                l.SetGap(8.0f);
              },
              Label::BasicLabel("Max Z-Depth:",
                                [](Label& l) { l.SetColor(0xFF4B5563); }),
              Slider::BasicSlider(0.0f, static_cast<float>(max_z_depth_),
                                  static_cast<float>(max_z_depth_),
                                  [this](float val) {
                                    max_z_depth_ = static_cast<int>(val);
                                    if (max_z_depth_label_ &&
                                        max_z_depth_label_->Get<Label>())
                                      max_z_depth_label_->Get<Label>()->SetText(
                                          std::to_string(max_z_depth_));
                                    canvas_->Invalidate();
                                  }),
              max_z_depth_label_),
          // 3D Mode Offset
          Container::HorizontalContainer(
              [](Layout& l) {
                l.SetAlignItems(YGAlignCenter);
                l.SetGap(8.0f);
              },
              Label::BasicLabel("3D Mode:"),
              Slider::BasicSlider(0.0f, 50.0f, 0.0f,
                                  [this](float val) {
                                    mode_3d_offset_ = val;
                                    canvas_->Invalidate();
                                  })),
          // Zoom Controls
          Container::HorizontalContainer(
              [](Layout& l) {
                l.SetAlignItems(YGAlignCenter);
                l.SetGap(4.0f);
              },
              Label::BasicLabel("Zoom:"),
              Button::TextButton("-",
                                 [this]() {
                                   scale_ = std::max(0.1f, scale_ / 1.25f);
                                   canvas_->Invalidate();
                                 }),
              Button::TextButton("1:1",
                                 [this]() {
                                   scale_ = 1.0f;
                                   pan_x_ = 0.0f;
                                   pan_y_ = 0.0f;
                                   canvas_->Invalidate();
                                 }),
              Button::TextButton("+",
                                 [this]() {
                                   scale_ = std::min(10.0f, scale_ * 1.25f);
                                   canvas_->Invalidate();
                                 }))),
      // Workspace
      ResizableContainer::HorizontalContainer(
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
            layout.SetMinHeight(0.0f);
            layout.SetWidthPercent(100.0f);
          },
          tree_panel, canvas_, inspector_panel_->GetUiNode()));
}

UIDebuggerWindow::~UIDebuggerWindow() {
  if (is_live_ && disappearance_message_id_ != 0) {
    live_window_.StopNotifyingOnDisappearance(disappearance_message_id_);
  }
}

void UIDebuggerWindow::LeaveLiveMode() {
  if (!is_live_) return;
  is_live_ = false;
  if (disappearance_message_id_ != 0) {
    live_window_.StopNotifyingOnDisappearance(disappearance_message_id_);
    disappearance_message_id_ = 0;
  }
  if (live_controls_) {
    if (auto parent = live_controls_->GetParent().lock()) {
      parent->RemoveChild(live_controls_);
    }
    live_controls_.reset();
  }
  UpdateInspector();
  if (main_window_) main_window_->Invalidate();
}

void UIDebuggerWindow::RebuildTree() {
  if (!tree_container_) return;
  auto tv = tree_container_->Get<TreeView>();
  if (tv && tv->GetContentContainer()) {
    auto content = tv->GetContentContainer();
    content->RemoveChildren();
    if (root_node_) {
      if (auto item = BuildTree(root_node_)) {
        content->AddChild(item);
      }
    }
  }
}

std::shared_ptr<Node> UIDebuggerWindow::BuildTree(
    std::shared_ptr<InspectedNode> node) {
  if (!node) return nullptr;

  bool this_matches = MatchesQuery(node, search_query_);
  bool descendant_matches = HasMatchingDescendant(node, search_query_);

  if (!search_query_.empty() && !this_matches && !descendant_matches) {
    return nullptr;
  }

  std::string label_text = node->name;
  if (node->is_hidden) label_text += " [Hidden]";

  std::vector<std::shared_ptr<Node>> children;
  for (auto& child : node->children) {
    if (auto child_node = BuildTree(child)) {
      children.push_back(child_node);
    }
  }

  auto item = TreeViewItem::Item(label_text, children);

  if (auto tv_item = item->Get<TreeViewItem>()) {
    tv_item->OnSelect([this, node]() {
      selected_node_ = node;
      UpdateInspector();
      canvas_->Invalidate();
    });
    tv_item->OnContextMenu([this, node](Point screen_pt) {
      ShowTreeContextMenu(node, screen_pt);
    });
    if (node->z_depth == 0 || (!search_query_.empty() && descendant_matches)) {
      tv_item->SetExpanded(true);
    }
    node->tree_item = tv_item;
  }

  return item;
}

void UIDebuggerWindow::DrawCanvas(const DrawContext& dc) {
  dc.skia_canvas->save();
  dc.skia_canvas->clipRect(SkRect::MakeXYWH(dc.area.origin.x, dc.area.origin.y,
                                            dc.area.size.width,
                                            dc.area.size.height));

  if (root_node_) {
    DrawNode(root_node_, dc);
  }

  DrawMeasurementGuides(dc);

  dc.skia_canvas->restore();
}

void UIDebuggerWindow::DrawNode(std::shared_ptr<InspectedNode> node,
                                const DrawContext& dc) {
  if (!node || node->is_hidden) return;
  if (node->z_depth > max_z_depth_) return;

  SkRect dest = GetNodeScreenRect(node, dc.area.origin.x, dc.area.origin.y,
                                  pan_x_, pan_y_, mode_3d_offset_, scale_);

  if (!node->texture.empty()) {
    dc.skia_canvas->drawImageRect(node->texture.asImage(), dest,
                                  SkSamplingOptions());
  }

  if (draw_border_) {
    SkPaint bp;
    bp.setColor(0x80000000);
    bp.setStyle(SkPaint::kStroke_Style);
    bp.setStrokeWidth(1.0f);
    dc.skia_canvas->drawRect(dest, bp);
  }

  auto sel = selected_node_.lock();
  if (sel == node) {
    SkPaint sp;
    sp.setColor(0xFF3B82F6);
    sp.setStyle(SkPaint::kStroke_Style);
    sp.setStrokeWidth(3.0f);
    dc.skia_canvas->drawRect(dest, sp);
  } else if (!search_query_.empty() && MatchesQuery(node, search_query_)) {
    SkPaint hp;
    hp.setColor(0xFFF59E0B);
    hp.setStyle(SkPaint::kStroke_Style);
    hp.setStrokeWidth(2.5f);
    dc.skia_canvas->drawRect(dest, hp);
  }

  for (auto& child : node->children) {
    DrawNode(child, dc);
  }
}

void UIDebuggerWindow::DrawMeasurementGuides(const DrawContext& dc) {
  auto sel = selected_node_.lock();
  auto hov = hovered_node_.lock();

  if (!sel || !hov || sel == hov || sel->is_hidden || hov->is_hidden ||
      sel->z_depth > max_z_depth_ || hov->z_depth > max_z_depth_) {
    return;
  }

  SkRect s = GetNodeScreenRect(sel, dc.area.origin.x, dc.area.origin.y, pan_x_,
                               pan_y_, mode_3d_offset_, scale_);
  SkRect h = GetNodeScreenRect(hov, dc.area.origin.x, dc.area.origin.y, pan_x_,
                               pan_y_, mode_3d_offset_, scale_);

  // Outline hovered node in crisp crimson
  SkPaint hp;
  hp.setColor(0xFFE11D48);
  hp.setStyle(SkPaint::kStroke_Style);
  hp.setStrokeWidth(2.0f);
  dc.skia_canvas->drawRect(h, hp);

  // Determine geometry separation
  bool overlap_x = (s.left() < h.right() && h.left() < s.right());
  bool overlap_y = (s.top() < h.bottom() && h.top() < s.bottom());

  if (overlap_x) {
    float mid_x =
        (std::max(s.left(), h.left()) + std::min(s.right(), h.right())) / 2.0f;

    if (s.top() != h.top()) {
      DrawDimensionLineAndBadge(dc.skia_canvas, Point{mid_x, h.top()},
                                Point{mid_x, s.top()},
                                std::abs(s.top() - h.top()) / scale_);
    }
    if (s.bottom() != h.bottom()) {
      DrawDimensionLineAndBadge(dc.skia_canvas, Point{mid_x, s.bottom()},
                                Point{mid_x, h.bottom()},
                                std::abs(s.bottom() - h.bottom()) / scale_);
    }
  } else {
    float mid_y = (s.top() + s.bottom()) / 2.0f;
    if (s.right() <= h.left()) {
      DrawDimensionLineAndBadge(dc.skia_canvas, Point{s.right(), mid_y},
                                Point{h.left(), mid_y},
                                (h.left() - s.right()) / scale_);
    } else if (h.right() <= s.left()) {
      DrawDimensionLineAndBadge(dc.skia_canvas, Point{h.right(), mid_y},
                                Point{s.left(), mid_y},
                                (s.left() - h.right()) / scale_);
    }
  }

  if (overlap_y) {
    float mid_y =
        (std::max(s.top(), h.top()) + std::min(s.bottom(), h.bottom())) / 2.0f;

    if (s.left() != h.left()) {
      DrawDimensionLineAndBadge(dc.skia_canvas, Point{h.left(), mid_y},
                                Point{s.left(), mid_y},
                                std::abs(s.left() - h.left()) / scale_);
    }
    if (s.right() != h.right()) {
      DrawDimensionLineAndBadge(dc.skia_canvas, Point{s.right(), mid_y},
                                Point{h.right(), mid_y},
                                std::abs(s.right() - h.right()) / scale_);
    }
  } else {
    float mid_x = (s.left() + s.right()) / 2.0f;
    if (s.bottom() <= h.top()) {
      DrawDimensionLineAndBadge(dc.skia_canvas, Point{mid_x, s.bottom()},
                                Point{mid_x, h.top()},
                                (h.top() - s.bottom()) / scale_);
    } else if (h.bottom() <= s.top()) {
      DrawDimensionLineAndBadge(dc.skia_canvas, Point{mid_x, h.bottom()},
                                Point{mid_x, s.top()},
                                (s.top() - h.bottom()) / scale_);
    }
  }
}

std::shared_ptr<InspectedNode> UIDebuggerWindow::HitTestNode(
    std::shared_ptr<InspectedNode> node, Point pt) {
  if (!node || node->is_hidden || node->z_depth > max_z_depth_) return nullptr;

  for (auto itr = node->children.rbegin(); itr != node->children.rend();
       ++itr) {
    auto hit = HitTestNode(*itr, pt);
    if (hit) return hit;
  }

  float rx =
      pan_x_ +
      (node->global_position.x + mode_3d_offset_ * node->z_depth) * scale_;
  float ry =
      pan_y_ +
      (node->global_position.y + mode_3d_offset_ * node->z_depth) * scale_;
  float rw = node->size.width * scale_;
  float rh = node->size.height * scale_;

  if (rx <= pt.x && pt.x <= rx + rw && ry <= pt.y && pt.y <= ry + rh) {
    return node;
  }

  return nullptr;
}

void UIDebuggerWindow::UpdateInspector() {
  if (inspector_panel_) {
    inspector_panel_->Update(selected_node_.lock(), is_live_);
  }
}

void UIDebuggerWindow::TweakNodeProperty(std::string_view node_id,
                                         std::string_view property_name,
                                         std::string_view property_value) {
  if (is_live_) {
    try {
      ::perception::window::TweakUiRequest request;
      ::perception::window::NodePropertyTweak tweak;
      if (!node_id.empty()) {
        char* end_ptr = nullptr;
        std::string s(node_id);
        tweak.node_id = std::strtoull(s.c_str(), &end_ptr, 0);
      }
      tweak.property_name = property_name;
      tweak.property_value = property_value;
      request.property_tweaks.push_back(tweak);
      live_window_.TweakUi(request);
    } catch (...) {
    }
  }

  try {
    if (auto node = selected_node_.lock()) {
      if (property_name == "hidden") {
        node->is_hidden = (property_value == "true" ||
                           property_value == "True" || property_value == "1");
      } else if (property_name.size() > 7 &&
                 property_name.substr(0, 7) == "layout.") {
        std::string prop = std::string(property_name.substr(7));
        bool found = false;
        for (auto& lp : node->layout_properties) {
          if (lp.first == prop) {
            lp.second = property_value;
            found = true;
            break;
          }
        }
        if (!found) {
          node->layout_properties.push_back(
              {prop, std::string(property_value)});
        }
      } else {
        node->component_properties[std::string(property_name)] = property_value;
      }
    }
    if (canvas_) canvas_->Invalidate();
  } catch (...) {
  }
}

void UIDebuggerWindow::CenterNodeInView(std::shared_ptr<InspectedNode> node) {
  if (!node || !canvas_) return;

  auto size = canvas_->GetSize();
  float* pan[2] = {&pan_x_, &pan_y_};
  float canvas_dim[2] = {size.width, size.height};

  for (int d = 0; d < 2; d++) {
    float node_center =
        (node->global_position[d] + mode_3d_offset_ * node->z_depth +
         node->size[d] / 2.0f) *
        scale_;
    *pan[d] = (canvas_dim[d] / 2.0f) - node_center;
  }

  canvas_->Invalidate();
}

void UIDebuggerWindow::ShowTreeContextMenu(std::shared_ptr<InspectedNode> node,
                                           Point abs_pt) {
  if (!main_window_ || !node) return;

  std::vector<std::shared_ptr<Node>> extra_items;
  if (!node->children.empty()) {
    extra_items.push_back(
        PopUpMenu::ContextMenuItem("Expand all children", [node]() {
          auto exp_func = [](auto& self,
                             std::shared_ptr<InspectedNode> n) -> void {
            if (!n) return;
            if (auto item = n->tree_item.lock()) item->SetExpanded(true);
            for (auto& c : n->children) self(self, c);
          };
          exp_func(exp_func, node);
        }));
    extra_items.push_back(
        PopUpMenu::ContextMenuItem("Collapse all children", [node]() {
          auto col_func = [](auto& self,
                             std::shared_ptr<InspectedNode> n) -> void {
            if (!n) return;
            if (auto item = n->tree_item.lock()) item->SetExpanded(false);
            for (auto& c : n->children) self(self, c);
          };
          col_func(col_func, node);
        }));
  }

  auto menu = PopUpMenu::Container(
      [](Layout& l) { l.SetWidth(170.0f); },
      PopUpMenu::ContextMenuItem("Center node in view",
                                 [this, node]() { CenterNodeInView(node); }),
      PopUpMenu::ContextMenuItem("Add child node",
                                 [this, node]() { CreateChildNode(node); }),
      PopUpMenu::ContextMenuItem("Delete node",
                                 [this, node]() { DeleteInspectedNode(node); }),
      extra_items);

  PopUp::Show(main_window_, abs_pt, menu);
}

std::shared_ptr<InspectedNode> UIDebuggerWindow::FindNodeById(
    std::shared_ptr<InspectedNode> node, std::string_view id) {
  if (!node || id.empty()) return nullptr;
  if (node->id == id) return node;
  for (auto& child : node->children) {
    if (auto res = FindNodeById(child, id)) return res;
  }
  return nullptr;
}

void UIDebuggerWindow::RefreshHierarchyAndSelect(std::string_view select_id) {
  if (!is_live_) return;
  auto hierarchy = live_window_.GetUiHierarchy();
  if (hierarchy) {
    try {
      ::nlohmann::json data = ::nlohmann::json::parse(hierarchy->json);
      root_node_ = ParseInspectedNode(data, 0, ::perception::ui::Point{0, 0},
                                      ::perception::ui::Point{0, 0});
      RebuildTree();
      if (!select_id.empty()) {
        if (auto node = FindNodeById(root_node_, select_id)) {
          selected_node_ = node;
          UpdateInspector();
          if (auto item = node->tree_item.lock()) {
            item->Select(true, false);
          }
        }
      } else {
        selected_node_ = {};
        UpdateInspector();
      }
      if (canvas_) canvas_->Invalidate();
    } catch (...) {
    }
  }
}

void UIDebuggerWindow::CreateChildNode(
    std::shared_ptr<InspectedNode> parent_node) {
  if (!parent_node) return;
  uint64 temp_id = next_temp_id_++;

  if (is_live_) {
    try {
      ::perception::window::TweakUiRequest request;
      ::perception::window::CreateNode create_entry;
      create_entry.temp_id = temp_id;
      char* end_ptr = nullptr;
      std::string s(parent_node->id);
      create_entry.parent_node_id = std::strtoull(s.c_str(), &end_ptr, 0);
      request.create_nodes.push_back(create_entry);

      auto status_or_resp = live_window_.TweakUi(request);
      uint64 real_id = 0;
      if (status_or_resp && !status_or_resp->create_node_responses.empty()) {
        real_id = status_or_resp->create_node_responses.front().real_id;
      }
      std::string select_id = "";
      if (real_id != 0) {
        std::stringstream ss;
        ss << "0x" << std::hex << real_id;
        select_id = ss.str();
      }
      RefreshHierarchyAndSelect(select_id);
    } catch (...) {
    }
    return;
  }

  auto child = std::make_shared<InspectedNode>();
  child->id = std::to_string(temp_id);
  child->name = "Node";
  child->z_depth = parent_node->z_depth + 1;
  child->parent = parent_node;
  parent_node->children.push_back(child);
  RebuildTree();
  selected_node_ = child;
  UpdateInspector();
  if (auto item = child->tree_item.lock()) {
    item->Select(true, false);
  }
  if (canvas_) canvas_->Invalidate();
}

void UIDebuggerWindow::DeleteInspectedNode(
    std::shared_ptr<InspectedNode> node) {
  if (!node) return;
  if (is_live_) {
    try {
      ::perception::window::TweakUiRequest request;
      ::perception::window::DeleteNode del_entry;
      char* end_ptr = nullptr;
      std::string s(node->id);
      del_entry.node_id = std::strtoull(s.c_str(), &end_ptr, 0);
      request.delete_nodes.push_back(del_entry);
      live_window_.TweakUi(request);
      RefreshHierarchyAndSelect("");
    } catch (...) {
    }
    return;
  }

  if (node == root_node_) {
    root_node_.reset();
    selected_node_.reset();
  } else if (auto p = node->parent.lock()) {
    p->children.erase(std::remove(p->children.begin(), p->children.end(), node),
                      p->children.end());
    if (selected_node_.lock() == node) {
      selected_node_ = p;
    }
  }
  RebuildTree();
  UpdateInspector();
  if (canvas_) canvas_->Invalidate();
}

bool UIDebuggerWindow::IsDescendantOf(std::shared_ptr<InspectedNode> node,
                                      std::shared_ptr<InspectedNode> ancestor) {
  auto curr = node;
  while (curr) {
    if (curr == ancestor) return true;
    curr = curr->parent.lock();
  }
  return false;
}

void UIDebuggerWindow::ReparentInspectedNode(
    std::shared_ptr<InspectedNode> node,
    std::shared_ptr<InspectedNode> new_parent) {
  if (!node || !new_parent || node == new_parent) return;
  if (IsDescendantOf(new_parent, node)) return;

  if (is_live_) {
    try {
      ::perception::window::TweakUiRequest request;
      ::perception::window::ReparentNode reparent_entry;
      char* end_ptr1 = nullptr;
      std::string s1(node->id);
      reparent_entry.node_id = std::strtoull(s1.c_str(), &end_ptr1, 0);
      char* end_ptr2 = nullptr;
      std::string s2(new_parent->id);
      reparent_entry.new_parent_node_id =
          std::strtoull(s2.c_str(), &end_ptr2, 0);
      request.reparent_nodes.push_back(reparent_entry);
      live_window_.TweakUi(request);
      RefreshHierarchyAndSelect(node->id);
    } catch (...) {
    }
    return;
  }

  if (auto old_p = node->parent.lock()) {
    old_p->children.erase(
        std::remove(old_p->children.begin(), old_p->children.end(), node),
        old_p->children.end());
  }
  node->parent = new_parent;
  new_parent->children.push_back(node);

  auto update_depths = [](auto& self, std::shared_ptr<InspectedNode> n,
                          int d) -> void {
    if (!n) return;
    n->z_depth = d;
    for (auto& c : n->children) self(self, c, d + 1);
  };
  update_depths(update_depths, node, new_parent->z_depth + 1);

  RebuildTree();
  if (canvas_) canvas_->Invalidate();
}
