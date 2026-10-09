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

#include "file_list_view.h"

#include <algorithm>
#include <cmath>
#include <system_error>
#include <utility>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "perception/file.h"
#include "perception/scheduler.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/focusable.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/file_icon.h"
#include "perception/ui/font.h"
#include "perception/ui/keyboard.h"
#include "perception/ui/layout.h"
#include "perception/ui/rectangle.h"
#include "perception/ui/text_alignment.h"
#include "perception/window/cursor.h"
#include "perception/window/mouse_button.h"

using ::perception::Defer;
using ::perception::FormatSize;
using ::perception::ui::CreateFileIcon;
using ::perception::ui::DrawContext;
using ::perception::ui::GetBold12UiFont;
using ::perception::ui::IsControlKey;
using ::perception::ui::IsShiftKey;
using ::perception::ui::KeyCode;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::Rectangle;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Container;
using ::perception::ui::components::Focusable;
using ::perception::ui::components::Label;
using ::perception::ui::components::ScrollContainer;
using ::perception::window::Cursor;
using ::perception::window::KeyboardKeyEvent;
using ::perception::window::MouseButton;

namespace {

// Width of the file size column.
constexpr float kSizeColumnWidth = 80.0f;

// Right margin after each file icon in a row.
constexpr float kIconMarginRight = 12.0f;

// Border radius of each file row.
constexpr float kRowBorderRadius = 6.0f;

// Horizontal padding inside each file row.
constexpr float kRowHorizontalPadding = 12.0f;

// Vertical padding inside each file row.
constexpr float kRowVerticalPadding = 8.0f;

// Left padding of the column headers container.
constexpr float kHeaderPaddingLeft = 18.0f;

// Right padding of the column headers container.
constexpr float kHeaderPaddingRight = 30.0f;

// Top padding of the column headers container.
constexpr float kHeaderPaddingTop = 6.0f;

// Bottom padding of the column headers container.
constexpr float kHeaderPaddingBottom = 2.0f;

// Padding around the interior of the files list container.
constexpr float kListPadding = 6.0f;

// Vertical gap between rows in the files list container.
constexpr float kListGap = 4.0f;

// Minimum drag distance in pixels before marquee selection begins.
constexpr float kDragThreshold = 4.0f;

// Maximum time in milliseconds between two clicks to count as a double-click.
constexpr int64_t kDoubleClickThresholdMs = 500;

// Background color of a hovered unselected row.
constexpr uint32 kRowHoverColor = SkColorSetARGB(0xFF, 0xE5, 0xE7, 0xEB);

// Background color of a selected row.
constexpr uint32 kRowSelectedColor = SkColorSetARGB(0xFF, 0xE0, 0xE7, 0xFF);

// Background color of a hovered selected row.
constexpr uint32 kRowSelectedHoverColor =
    SkColorSetARGB(0xFF, 0xC7, 0xD2, 0xFE);

// Fill color of the rubber-band marquee selection rectangle.
constexpr uint32 kMarqueeFillColor = SkColorSetARGB(0x33, 0x3B, 0x82, 0xF6);

// Border color of the rubber-band marquee selection rectangle.
constexpr uint32 kMarqueeBorderColor = SkColorSetARGB(0xCC, 0x25, 0x63, 0xEB);

// Primary column header text color.
constexpr uint32 kHeaderTextColor = 0xFF4B5563;

// Active sorted column header text color.
constexpr uint32 kHeaderActiveColor = 0xFF111827;

// Primary item filename label color.
constexpr uint32 kItemNameColor = 0xFF1F2937;

// Secondary label color for file sizes.
constexpr uint32 kSecondaryLabelColor = 0xFF6B7280;

Rectangle MakeNormalizedRectangle(const Point& a, const Point& b) {
  Point min_pt{.x = std::min(a.x, b.x), .y = std::min(a.y, b.y)};
  Point max_pt{.x = std::max(a.x, b.x), .y = std::max(a.y, b.y)};
  return Rectangle::FromMinMaxPoints(min_pt, max_pt);
}

void UpdateSingleHeaderLabel(const std::shared_ptr<Node>& header_node,
                             std::string_view base_title, bool is_active,
                             SortDirection direction) {
  if (!header_node) return;
  std::string text(base_title);
  if (is_active)
    text += (direction == SortDirection::ASCENDING) ? " ▲" : " ▼";
  auto label = header_node->Get<Label>();
  label->SetText(text);
  label->SetColor(is_active ? kHeaderActiveColor : kHeaderTextColor);
  header_node->Invalidate();
}

}  // namespace

FileListView::FileListView(FileListCallbacks callbacks)
    : callbacks_(std::move(callbacks)) {
  BuildUi();
}

void FileListView::Focus() {
  if (!files_scroll_container_) return;
  if (auto focusable = files_scroll_container_->Get<Focusable>())
    focusable->Focus();
}

void FileListView::SetSortOrder(SortColumn column, SortDirection direction) {
  sort_column_ = column;
  sort_direction_ = direction;
  UpdateHeaderLabels();
}

void FileListView::SetEntries(
    std::vector<std::filesystem::directory_entry> entries,
    const std::set<std::string>& paths_to_select) {
  current_items_ = std::move(entries);
  hovered_index_ = -1;
  is_mouse_down_in_list_ = false;
  is_drag_selecting_ = false;

  selected_paths_.clear();
  anchor_index_ = -1;
  focus_index_ = -1;
  for (size_t i = 0; i < current_items_.size(); i++) {
    std::string p = current_items_[i].path().string();
    if (paths_to_select.count(p)) {
      selected_paths_.insert(p);
      if (anchor_index_ < 0) anchor_index_ = static_cast<int>(i);
      focus_index_ = static_cast<int>(i);
    }
  }

  UpdateHeaderLabels();

  if (files_list_container_) {
    files_list_container_->RemoveChildren();
    row_nodes_.clear();

    std::error_code ec;
    for (size_t i = 0; i < current_items_.size(); i++) {
      const auto& entry = current_items_[i];
      std::string name = entry.path().filename().string();
      std::string entry_path = entry.path().string();
      bool is_dir = entry.is_directory();
      int row_index = static_cast<int>(i);

      bool is_symlink = entry.is_symlink(ec);
      auto icon = CreateFileIcon(is_dir, is_symlink, name);
      icon->GetLayout().SetMargin(YGEdgeRight, kIconMarginRight);

      std::string size_str = "";
      if (!is_dir) {
        std::error_code size_ec;
        uintmax_t file_size = entry.file_size(size_ec);
        if (!size_ec) size_str = FormatSize(file_size);
      }

      auto row = Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetAlignItems(YGAlignCenter);
            layout.SetPadding(YGEdgeHorizontal, kRowHorizontalPadding);
            layout.SetPadding(YGEdgeVertical, kRowVerticalPadding);
          },
          [](Block& block) {
            block.SetBorderRadius(kRowBorderRadius);
            block.SetFillColor(0);
          },
          [this, entry_path, is_dir, row_index](Node& node) {
            auto* row_ptr = &node;
            node.SetCursor(Cursor::Poke);
            node.OnMouseHover([this, row_ptr, row_index](const Point& point) {
              if (is_mouse_down_in_list_) {
                HandleListMouseDrag(*row_ptr, point);
                return;
              }
              if (hovered_index_ != row_index) {
                hovered_index_ = row_index;
                UpdateRowVisuals();
              }
            });
            node.OnMouseLeave([this, row_index]() {
              if (!is_mouse_down_in_list_ && hovered_index_ == row_index) {
                hovered_index_ = -1;
                UpdateRowVisuals();
              }
            });
            node.OnMouseButtonDown(
                [this, row_ptr, entry_path, is_dir, row_index](
                    const Point& point, MouseButton button) {
                  row_mouse_down_handled_ = true;
                  Focus();

                  if (button == MouseButton::Left) {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed_ms =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - last_click_time_)
                            .count();

                    if (!ctrl_pressed_ && !shift_pressed_ &&
                        last_clicked_path_ == entry_path &&
                        elapsed_ms < kDoubleClickThresholdMs) {
                      last_clicked_path_.clear();
                      is_mouse_down_in_list_ = false;
                      if (callbacks_.on_open_item)
                        callbacks_.on_open_item(entry_path, is_dir);
                      return;
                    }

                    last_clicked_path_ = entry_path;
                    last_click_time_ = now;

                    if (shift_pressed_) {
                      int start_anchor =
                          (anchor_index_ >= 0 &&
                           anchor_index_ <
                               static_cast<int>(current_items_.size()))
                              ? anchor_index_
                              : row_index;
                      if (!ctrl_pressed_) selected_paths_.clear();
                      int low = std::min(start_anchor, row_index);
                      int high = std::max(start_anchor, row_index);
                      for (int idx = low; idx <= high; idx++)
                        selected_paths_.insert(
                            current_items_[idx].path().string());
                      if (anchor_index_ < 0) anchor_index_ = row_index;
                      focus_index_ = row_index;
                    } else if (ctrl_pressed_) {
                      if (selected_paths_.count(entry_path)) {
                        selected_paths_.erase(entry_path);
                      } else {
                        selected_paths_.insert(entry_path);
                      }
                      anchor_index_ = row_index;
                      focus_index_ = row_index;
                    } else {
                      selected_paths_ = {entry_path};
                      anchor_index_ = row_index;
                      focus_index_ = row_index;
                    }

                    UpdateRowVisuals();
                    if (callbacks_.on_selection_changed)
                      callbacks_.on_selection_changed();
                    BeginListMouseDrag(*row_ptr, point);
                  } else if (button == MouseButton::Right) {
                    item_context_menu_shown_ = true;
                    if (!selected_paths_.count(entry_path)) {
                      selected_paths_ = {entry_path};
                      anchor_index_ = row_index;
                      focus_index_ = row_index;
                      UpdateRowVisuals();
                      if (callbacks_.on_selection_changed)
                        callbacks_.on_selection_changed();
                    }
                    if (callbacks_.on_item_context_menu) {
                      callbacks_.on_item_context_menu(*row_ptr, point,
                                                      entry_path, is_dir);
                    }
                  }
                });
            node.OnMouseButtonUp(
                [this](const Point&, MouseButton button) {
                  if (button == MouseButton::Left) EndListMouseDrag();
                });
          },
          icon,
          Label::BasicLabel(
              name,
              [](Layout& layout) {
                layout.SetFlexGrow(1.0f);
                layout.SetFlexShrink(1.0f);
              },
              [](Label& label) {
                label.SetTextAlignment(TextAlignment::MiddleLeft);
                label.SetColor(kItemNameColor);
              }),
          Label::BasicLabel(
              size_str,
              [](Layout& layout) {
                layout.SetWidth(kSizeColumnWidth);
                layout.SetFlexShrink(0.0f);
              },
              [](Label& label) {
                label.SetTextAlignment(TextAlignment::MiddleRight);
                label.SetColor(kSecondaryLabelColor);
              }));

      row_nodes_.push_back(row);
    }

    files_list_container_->AddChildren(row_nodes_);
    UpdateRowVisuals();
    files_list_container_->Invalidate();
    if (files_scroll_container_) files_scroll_container_->Invalidate();
  }

  if (callbacks_.on_selection_changed) callbacks_.on_selection_changed();
}

std::vector<std::string> FileListView::GetSelectedPathsInDisplayOrder() const {
  std::vector<std::string> ordered;
  for (const auto& entry : current_items_) {
    std::string p = entry.path().string();
    if (selected_paths_.count(p)) ordered.push_back(p);
  }
  if (ordered.empty())
    ordered.assign(selected_paths_.begin(), selected_paths_.end());
  return ordered;
}

void FileListView::SelectAll() {
  if (current_items_.empty()) return;
  selected_paths_.clear();
  for (const auto& entry : current_items_)
    selected_paths_.insert(entry.path().string());
  anchor_index_ = 0;
  focus_index_ = static_cast<int>(current_items_.size()) - 1;
  UpdateRowVisuals();
  if (callbacks_.on_selection_changed) callbacks_.on_selection_changed();
}

void FileListView::BuildUi() {
  auto headers = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetPadding(YGEdgeLeft, kHeaderPaddingLeft);
        layout.SetPadding(YGEdgeRight, kHeaderPaddingRight);
        layout.SetPadding(YGEdgeTop, kHeaderPaddingTop);
        layout.SetPadding(YGEdgeBottom, kHeaderPaddingBottom);
      },
      Label::BasicLabel(
          "Name ▲", [](Layout& layout) { layout.SetFlexGrow(1.0f); },
          [](Label& label) {
            label.SetTextAlignment(TextAlignment::MiddleLeft);
            label.SetColor(kHeaderActiveColor);
            label.SetFont(GetBold12UiFont());
          },
          [this](Node& node) {
            name_header_label_ = node.shared_from_this();
            node.SetCursor(Cursor::Poke);
            node.OnMouseButtonDown(
                [this](const Point&, MouseButton button) {
                  if (button == MouseButton::Left) {
                    if (sort_column_ == SortColumn::NAME) {
                      sort_direction_ =
                          (sort_direction_ == SortDirection::ASCENDING)
                              ? SortDirection::DESCENDING
                              : SortDirection::ASCENDING;
                    } else {
                      sort_column_ = SortColumn::NAME;
                      sort_direction_ = SortDirection::ASCENDING;
                    }
                    UpdateHeaderLabels();
                    if (callbacks_.on_sort_changed) {
                      SortColumn col = sort_column_;
                      SortDirection dir = sort_direction_;
                      Defer([this, col, dir]() {
                        callbacks_.on_sort_changed(col, dir);
                      });
                    }
                  }
                });
          }),
      Label::BasicLabel(
          "Size",
          [](Layout& layout) { layout.SetWidth(kSizeColumnWidth); },
          [](Label& label) {
            label.SetTextAlignment(TextAlignment::MiddleRight);
            label.SetColor(kHeaderTextColor);
            label.SetFont(GetBold12UiFont());
          },
          [this](Node& node) {
            size_header_label_ = node.shared_from_this();
            node.SetCursor(Cursor::Poke);
            node.OnMouseButtonDown(
                [this](const Point&, MouseButton button) {
                  if (button == MouseButton::Left) {
                    if (sort_column_ == SortColumn::SIZE) {
                      sort_direction_ =
                          (sort_direction_ == SortDirection::ASCENDING)
                              ? SortDirection::DESCENDING
                              : SortDirection::ASCENDING;
                    } else {
                      sort_column_ = SortColumn::SIZE;
                      sort_direction_ = SortDirection::ASCENDING;
                    }
                    UpdateHeaderLabels();
                    if (callbacks_.on_sort_changed) {
                      SortColumn col = sort_column_;
                      SortDirection dir = sort_direction_;
                      Defer([this, col, dir]() {
                        callbacks_.on_sort_changed(col, dir);
                      });
                    }
                  }
                });
          }));

  auto scroll_view = ScrollContainer::VerticalScrollContainer(
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetMinHeightPercent(100.0f);
            layout.SetPadding(YGEdgeAll, kListPadding);
            layout.SetGap(kListGap);
          },
          [this](Node& node) {
            files_list_container_ = node.shared_from_this();
            node.OnDrawPostChildren([this](const DrawContext& context) {
              if (!is_drag_selecting_) return;
              Rectangle marquee = MakeNormalizedRectangle(
                  drag_start_in_list_, drag_current_in_list_);
              SkRect rect = SkRect::MakeXYWH(
                  context.area.origin.x + marquee.MinX(),
                  context.area.origin.y + marquee.MinY(), marquee.Width(),
                  marquee.Height());

              SkPaint fill_paint;
              fill_paint.setAntiAlias(true);
              fill_paint.setStyle(SkPaint::kFill_Style);
              fill_paint.setColor(kMarqueeFillColor);
              context.skia_canvas->drawRect(rect, fill_paint);

              SkPaint border_paint;
              border_paint.setAntiAlias(true);
              border_paint.setStyle(SkPaint::kStroke_Style);
              border_paint.setStrokeWidth(1.0f);
              border_paint.setColor(kMarqueeBorderColor);
              context.skia_canvas->drawRect(rect, border_paint);
            });
          }),
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetMinHeight(0.0f);
        layout.SetWidthPercent(100.0f);
      },
      [this](Focusable& focusable) {
        focusable.OnKeyDown(
            [this](const KeyboardKeyEvent& event) { HandleKeyDown(event); });
        focusable.OnKeyUp(
            [this](const KeyboardKeyEvent& event) { HandleKeyUp(event); });
        focusable.OnUnfocus([this]() {
          ctrl_pressed_ = false;
          shift_pressed_ = false;
          EndListMouseDrag();
        });
      },
      [this](Node& node) {
        auto* scroll_ptr = &node;
        files_scroll_container_ = node.shared_from_this();
        node.SetCursor(Cursor::Pointer);
        node.OnMouseHover([this, scroll_ptr](const Point& point) {
          if (is_mouse_down_in_list_) HandleListMouseDrag(*scroll_ptr, point);
        });
        node.OnMouseLeave([this]() {
          if (is_mouse_down_in_list_) EndListMouseDrag();
        });
        node.OnMouseButtonDown(
            [this, scroll_ptr](const Point& point, MouseButton button) {
              if (row_mouse_down_handled_) {
                row_mouse_down_handled_ = false;
                item_context_menu_shown_ = false;
                return;
              }
              if (auto sc = scroll_ptr->Get<ScrollContainer>())
                if (point.x >= sc->ContainerSize().width) return;
              Focus();
              if (button == MouseButton::Left) {
                last_clicked_path_.clear();
                if (!ctrl_pressed_ && !shift_pressed_) {
                  selected_paths_.clear();
                  anchor_index_ = -1;
                  focus_index_ = -1;
                  UpdateRowVisuals();
                  if (callbacks_.on_selection_changed)
                    callbacks_.on_selection_changed();
                }
                BeginListMouseDrag(*scroll_ptr, point);
              } else if (button == MouseButton::Right) {
                if (item_context_menu_shown_) {
                  item_context_menu_shown_ = false;
                  return;
                }
                if (!ctrl_pressed_ && !shift_pressed_) {
                  selected_paths_.clear();
                  anchor_index_ = -1;
                  focus_index_ = -1;
                  UpdateRowVisuals();
                  if (callbacks_.on_selection_changed)
                    callbacks_.on_selection_changed();
                }
                if (callbacks_.on_background_context_menu)
                  callbacks_.on_background_context_menu(*scroll_ptr, point);
              } else {
                item_context_menu_shown_ = false;
              }
            });
        node.OnMouseButtonUp([this](const Point&, MouseButton button) {
          if (button == MouseButton::Left) EndListMouseDrag();
        });
      });

  root_node_ = Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetMinHeight(0.0f);
        layout.SetWidthPercent(100.0f);
      },
      headers, scroll_view);
}

void FileListView::UpdateHeaderLabels() {
  UpdateSingleHeaderLabel(name_header_label_, "Name",
                          sort_column_ == SortColumn::NAME, sort_direction_);
  UpdateSingleHeaderLabel(size_header_label_, "Size",
                          sort_column_ == SortColumn::SIZE, sort_direction_);
}

void FileListView::UpdateRowVisuals() {
  for (size_t i = 0; i < row_nodes_.size() && i < current_items_.size(); i++) {
    auto block = row_nodes_[i]->Get<Block>();
    if (!block) continue;

    std::string path = current_items_[i].path().string();
    bool is_selected = selected_paths_.count(path) > 0;
    bool is_hovered =
        !is_drag_selecting_ && (static_cast<int>(i) == hovered_index_);

    uint32 target_color = 0;
    if (is_selected && is_hovered) {
      target_color = kRowSelectedHoverColor;
    } else if (is_selected) {
      target_color = kRowSelectedColor;
    } else if (is_hovered) {
      target_color = kRowHoverColor;
    }

    if (block->GetFillColor() != target_color) {
      block->SetFillColor(target_color);
      row_nodes_[i]->Invalidate();
    }
  }
}

void FileListView::SelectByIndex(int new_index, bool extend_range) {
  if (current_items_.empty()) return;
  int clamped =
      std::clamp(new_index, 0, static_cast<int>(current_items_.size()) - 1);

  if (extend_range) {
    int start_anchor =
        (anchor_index_ >= 0 &&
         anchor_index_ < static_cast<int>(current_items_.size()))
            ? anchor_index_
            : clamped;
    anchor_index_ = start_anchor;
    focus_index_ = clamped;
    selected_paths_.clear();
    int low = std::min(start_anchor, clamped);
    int high = std::max(start_anchor, clamped);
    for (int i = low; i <= high; i++)
      selected_paths_.insert(current_items_[i].path().string());
  } else {
    anchor_index_ = clamped;
    focus_index_ = clamped;
    selected_paths_ = {current_items_[clamped].path().string()};
  }

  UpdateRowVisuals();
  if (callbacks_.on_selection_changed) callbacks_.on_selection_changed();

  if (files_scroll_container_ &&
      clamped < static_cast<int>(row_nodes_.size())) {
    if (auto sc = files_scroll_container_->Get<ScrollContainer>())
      sc->ScrollIntoView(row_nodes_[clamped]);
  }
}

Point FileListView::PointInFilesList(Node& source_node,
                                     const Point& local_point) const {
  Point screen_pt = source_node.GetAbsolutePosition() + local_point;
  if (!files_list_container_) return screen_pt;
  return screen_pt - files_list_container_->GetAbsolutePosition();
}

void FileListView::BeginListMouseDrag(Node& source_node,
                                      const Point& local_point) {
  is_mouse_down_in_list_ = true;
  is_drag_selecting_ = false;
  drag_start_in_list_ = PointInFilesList(source_node, local_point);
  drag_current_in_list_ = drag_start_in_list_;
  if (ctrl_pressed_ || shift_pressed_) {
    drag_initial_selection_ = selected_paths_;
  } else {
    drag_initial_selection_.clear();
  }
}

void FileListView::HandleListMouseDrag(Node& source_node,
                                       const Point& local_point) {
  if (!is_mouse_down_in_list_ || !files_list_container_) return;

  Point current_pt = PointInFilesList(source_node, local_point);
  if (!is_drag_selecting_) {
    float dx = current_pt.x - drag_start_in_list_.x;
    float dy = current_pt.y - drag_start_in_list_.y;
    if (std::sqrt(dx * dx + dy * dy) < kDragThreshold) return;
    is_drag_selecting_ = true;
  }

  drag_current_in_list_ = current_pt;
  Rectangle marquee =
      MakeNormalizedRectangle(drag_start_in_list_, drag_current_in_list_);

  selected_paths_ = drag_initial_selection_;
  int last_intersected = -1;
  for (size_t i = 0; i < row_nodes_.size() && i < current_items_.size(); i++) {
    Rectangle row_rect = row_nodes_[i]->GetAreaRelativeToParent();
    if (row_rect.Intersects(marquee)) {
      std::string path = current_items_[i].path().string();
      if (ctrl_pressed_ && drag_initial_selection_.count(path)) {
        selected_paths_.erase(path);
      } else {
        selected_paths_.insert(path);
      }
      last_intersected = static_cast<int>(i);
    }
  }

  if (last_intersected >= 0) {
    focus_index_ = last_intersected;
    if (anchor_index_ < 0) anchor_index_ = last_intersected;
  }

  UpdateRowVisuals();
  if (callbacks_.on_selection_changed) callbacks_.on_selection_changed();
  files_list_container_->Invalidate();
  if (files_scroll_container_) files_scroll_container_->Invalidate();
}

void FileListView::EndListMouseDrag() {
  if (!is_mouse_down_in_list_) return;
  is_mouse_down_in_list_ = false;
  if (is_drag_selecting_) {
    is_drag_selecting_ = false;
    last_clicked_path_.clear();
    UpdateRowVisuals();
    if (files_list_container_) files_list_container_->Invalidate();
    if (files_scroll_container_) files_scroll_container_->Invalidate();
  }
}

void FileListView::HandleKeyDown(const KeyboardKeyEvent& event) {
  if (IsShiftKey(event.key)) {
    shift_pressed_ = true;
    return;
  }
  if (IsControlKey(event.key)) {
    ctrl_pressed_ = true;
    return;
  }

  KeyCode key = static_cast<KeyCode>(event.key);
  if (ctrl_pressed_) {
    if (key == KeyCode::A) {
      SelectAll();
    } else if (key == KeyCode::C) {
      if (callbacks_.on_copy) callbacks_.on_copy();
    } else if (key == KeyCode::X) {
      if (callbacks_.on_cut) callbacks_.on_cut();
    } else if (key == KeyCode::V) {
      if (callbacks_.on_paste) Defer([this]() { callbacks_.on_paste(); });
    } else if (key == KeyCode::D) {
      if (callbacks_.on_duplicate)
        Defer([this]() { callbacks_.on_duplicate(); });
    } else if (key == KeyCode::R) {
      if (callbacks_.on_refresh) Defer([this]() { callbacks_.on_refresh(); });
    }
    return;
  }

  switch (key) {
    case KeyCode::UpArrow:
      if (!current_items_.empty()) {
        int next = focus_index_ <= 0 ? 0 : focus_index_ - 1;
        SelectByIndex(next, shift_pressed_);
      }
      break;
    case KeyCode::DownArrow:
      if (!current_items_.empty()) {
        int next =
            focus_index_ < 0
                ? 0
                : std::min(static_cast<int>(current_items_.size()) - 1,
                           focus_index_ + 1);
        SelectByIndex(next, shift_pressed_);
      }
      break;
    case KeyCode::Home:
      if (!current_items_.empty()) SelectByIndex(0, shift_pressed_);
      break;
    case KeyCode::End:
      if (!current_items_.empty())
        SelectByIndex(static_cast<int>(current_items_.size()) - 1,
                      shift_pressed_);
      break;
    case KeyCode::Enter:
      if (callbacks_.on_open_selected_items)
        callbacks_.on_open_selected_items();
      break;
    case KeyCode::Backspace:
      if (callbacks_.on_navigate_up)
        Defer([this]() { callbacks_.on_navigate_up(); });
      break;
    case KeyCode::F2:
      if (selected_paths_.size() == 1 && callbacks_.on_rename)
        Defer([this]() { callbacks_.on_rename(); });
      break;
    case KeyCode::Delete:
      if (!selected_paths_.empty() && callbacks_.on_delete)
        Defer([this]() { callbacks_.on_delete(); });
      break;
    case KeyCode::F5:
      if (callbacks_.on_refresh) Defer([this]() { callbacks_.on_refresh(); });
      break;
    case KeyCode::Escape:
      selected_paths_.clear();
      anchor_index_ = -1;
      focus_index_ = -1;
      UpdateRowVisuals();
      if (callbacks_.on_selection_changed) callbacks_.on_selection_changed();
      break;
    default:
      break;
  }
}

void FileListView::HandleKeyUp(const KeyboardKeyEvent& event) {
  if (IsShiftKey(event.key)) shift_pressed_ = false;
  if (IsControlKey(event.key)) ctrl_pressed_ = false;
}
