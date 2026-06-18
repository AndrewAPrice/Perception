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

#include "perception/ui/components/table.h"

#include "perception/scheduler.h"
#include "perception/ui/theme.h"

namespace perception {

template class UniqueIdentifiableType<ui::components::Table>;

namespace ui {
namespace components {

Table::Table() {}

void Table::Initialize(std::shared_ptr<DataSource> data_source,
                       const std::vector<Column>& columns,
                       std::shared_ptr<Node> header_container,
                       std::shared_ptr<Node> rows_container) {
  data_source_ = data_source;
  columns_ = columns;
  header_container_ = header_container;
  rows_container_ = rows_container;
  BuildTable();
}

void Table::SetNode(std::weak_ptr<Node> node) { node_ = node; }

void Table::Refresh() {
  BuildTable();
  if (!node_.expired()) node_.lock()->Invalidate();
}

void Table::OnCellSelect(std::function<void(int, int)> on_cell_select) {
  on_cell_select_.push_back(on_cell_select);
}

void Table::OnCellHover(std::function<void(int, int)> on_cell_hover) {
  on_cell_hover_.push_back(on_cell_hover);
}

void Table::Sort(int column_index) {
  if (column_index < 0 || column_index >= static_cast<int>(columns_.size()))
    return;
  if (!columns_[column_index].sortable) return;

  if (sorted_column_index_ == column_index) {
    sort_ascending_ = !sort_ascending_;
  } else {
    sorted_column_index_ = column_index;
    sort_ascending_ = true;
  }

  data_source_->SortByColumn(column_index, sort_ascending_);
  Refresh();
}

void Table::BuildTable() {
  auto header = header_container_.lock();
  auto rows = rows_container_.lock();
  if (!header || !rows) return;

  hovered_row_index_ = -1;
  hovered_col_index_ = -1;

  header->RemoveChildren();
  rows->RemoveChildren();

  // Build Header cells
  std::vector<std::shared_ptr<Node>> header_cells;
  for (int i = 0; i < static_cast<int>(columns_.size()); ++i) {
    std::string title = columns_[i].title;
    if (i == sorted_column_index_) title += sort_ascending_ ? " ▲" : " ▼";

    auto header_cell_node = Node::Empty(
        columns_[i].layout_modifier,
        [](Layout& layout) {
          layout.SetPadding(YGEdgeHorizontal,
                            kTableHeaderCellHorizontalPadding);
        },
        Node::Empty([title](Label& label) {
          label.SetText(title);
          label.SetTextAlignment(TextAlignment::MiddleLeft);
          label.SetColor(kTableHeaderTextColor);
        }));

    if (columns_[i].sortable) {
      header_cell_node->OnMouseHover([header_cell_node](const Point& point) {
        for (auto& child : header_cell_node->GetChildren()) {
          if (auto label = child->Get<Label>()) {
            label->SetColor(kTableHeaderHoverTextColor);
          }
        }
      });
      header_cell_node->OnMouseLeave([header_cell_node]() {
        for (auto& child : header_cell_node->GetChildren()) {
          if (auto label = child->Get<Label>()) {
            label->SetColor(kTableHeaderTextColor);
          }
        }
      });
      header_cell_node->OnMouseButtonUp(
          [this, i](const Point& point, window::MouseButton button) {
            if (button == window::MouseButton::Left) {
              ::perception::Defer([this, i]() { this->Sort(i); });
            }
          });
    }

    header_cells.push_back(header_cell_node);
  }
  header->AddChildren(header_cells);

  // Build Rows
  int num_rows = data_source_->GetNumberOfRows();
  row_nodes_.clear();
  cell_nodes_.clear();
  cell_nodes_.resize(num_rows);

  std::vector<std::shared_ptr<Node>> row_nodes;

  for (int r = 0; r < num_rows; ++r) {
    std::vector<std::shared_ptr<Node>> row_cells;
    cell_nodes_[r].resize(columns_.size());

    for (int c = 0; c < static_cast<int>(columns_.size()); ++c) {
      std::string value = data_source_->GetCellValue(r, c);
      auto cell_node = Node::Empty(
          columns_[c].layout_modifier,
          [](Block& block) { block.SetFillColor(kTableCellTransparentColor); },
          [](Layout& layout) {
            layout.SetPadding(YGEdgeHorizontal, kTableCellHorizontalPadding);
            layout.SetPadding(YGEdgeVertical, kTableCellVerticalPadding);
          },
          Node::Empty([value](Label& label) {
            label.SetText(value);
            label.SetTextAlignment(TextAlignment::MiddleLeft);
            label.SetColor(kTableCellTextColor);
          }));

      cell_node->OnMouseHover(
          [this, r, c](const Point& point) { this->HoverCell(r, c); });
      cell_node->OnMouseLeave([this, r, c]() { this->LeaveCell(r, c); });
      cell_node->OnMouseButtonUp(
          [this, r, c](const Point& point, window::MouseButton button) {
            if (button == window::MouseButton::Left) {
              ::perception::Defer([this, r, c]() {
                for (auto& callback : on_cell_select_) {
                  callback(r, c);
                }
              });
            }
          });

      cell_nodes_[r][c] = cell_node;
      row_cells.push_back(cell_node);
    }

    auto row_node = Node::Empty(
        [](Block& block) { block.SetFillColor(kTableCellTransparentColor); },
        [](Layout& layout) {
          layout.SetFlexDirection(YGFlexDirectionRow);
          layout.SetWidthPercent(100.0f);
        });
    row_node->AddChildren(row_cells);
    row_nodes_.push_back(row_node);
    row_nodes.push_back(row_node);
  }
  rows->AddChildren(row_nodes);
}

void Table::HoverCell(int r, int c) {
  if (hovered_row_index_ == r && hovered_col_index_ == c) return;

  CellHighlightability new_selectability =
      data_source_->GetCellHighlightablity(r, c);
  CellHighlightability old_selectability =
      (hovered_row_index_ != -1 && hovered_col_index_ != -1)
          ? data_source_->GetCellHighlightablity(hovered_row_index_,
                                                 hovered_col_index_)
          : CellHighlightability::NotHighlightable;

  if (old_selectability == new_selectability) {
    if (new_selectability == CellHighlightability::RowHighlightable &&
        r == hovered_row_index_) {
      hovered_row_index_ = r;
      hovered_col_index_ = c;
      return;
    }
    if (new_selectability == CellHighlightability::ColumnHighlightable &&
        c == hovered_col_index_) {
      hovered_row_index_ = r;
      hovered_col_index_ = c;
      return;
    }
  }

  // Leave the previously hovered cell first if any
  if (hovered_row_index_ != -1 && hovered_col_index_ != -1) {
    LeaveCell(hovered_row_index_, hovered_col_index_);
  }

  if (new_selectability == CellHighlightability::NotHighlightable) return;

  hovered_row_index_ = r;
  hovered_col_index_ = c;

  for (auto& callback : on_cell_hover_) {
    callback(r, c);
  }

  if (new_selectability == CellHighlightability::CellHighlightable) {
    if (auto block = cell_nodes_[r][c]->Get<Block>()) {
      block->SetFillColor(kTableCellHighlightColor);
      cell_nodes_[r][c]->Invalidate();
    }
  } else if (new_selectability == CellHighlightability::RowHighlightable) {
    if (auto block = row_nodes_[r]->Get<Block>()) {
      block->SetFillColor(kTableCellHighlightColor);
      row_nodes_[r]->Invalidate();
    }
  } else if (new_selectability == CellHighlightability::ColumnHighlightable) {
    for (int row = 0; row < static_cast<int>(cell_nodes_.size()); ++row) {
      if (auto block = cell_nodes_[row][c]->Get<Block>()) {
        block->SetFillColor(kTableCellHighlightColor);
        cell_nodes_[row][c]->Invalidate();
      }
    }
  }
}

void Table::LeaveCell(int r, int c) {
  if (hovered_row_index_ == r && hovered_col_index_ == c) {
    hovered_row_index_ = -1;
    hovered_col_index_ = -1;
  } else {
    CellHighlightability old_selectability =
        data_source_->GetCellHighlightablity(r, c);
    if (hovered_row_index_ != -1 && hovered_col_index_ != -1) {
      CellHighlightability new_selectability =
          data_source_->GetCellHighlightablity(hovered_row_index_,
                                               hovered_col_index_);
      if (old_selectability == new_selectability) {
        if (old_selectability == CellHighlightability::RowHighlightable &&
            r == hovered_row_index_) {
          return;
        }
        if (old_selectability == CellHighlightability::ColumnHighlightable &&
            c == hovered_col_index_) {
          return;
        }
      }
    }
  }

  CellHighlightability selectability =
      data_source_->GetCellHighlightablity(r, c);
  if (selectability == CellHighlightability::NotHighlightable) return;

  if (selectability == CellHighlightability::CellHighlightable) {
    if (auto block = cell_nodes_[r][c]->Get<Block>()) {
      block->SetFillColor(kTableCellTransparentColor);
      cell_nodes_[r][c]->Invalidate();
    }
  } else if (selectability == CellHighlightability::RowHighlightable) {
    if (auto block = row_nodes_[r]->Get<Block>()) {
      block->SetFillColor(kTableCellTransparentColor);
      row_nodes_[r]->Invalidate();
    }
  } else if (selectability == CellHighlightability::ColumnHighlightable) {
    for (int row = 0; row < static_cast<int>(cell_nodes_.size()); ++row) {
      if (auto block = cell_nodes_[row][c]->Get<Block>()) {
        block->SetFillColor(kTableCellTransparentColor);
        cell_nodes_[row][c]->Invalidate();
      }
    }
  }
}

}  // namespace components
}  // namespace ui
}  // namespace perception
