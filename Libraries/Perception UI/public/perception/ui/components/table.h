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

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "perception/type_id.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_bar.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/theme.h"

namespace perception {
namespace ui {
namespace components {

enum class CellHighlightability {
  NotHighlightable,
  CellHighlightable,
  ColumnHighlightable,
  RowHighlightable
};

class Table : public UniqueIdentifiableType<Table> {
 public:
  struct Column {
    std::string title;
    std::function<void(Layout&)> layout_modifier;
    bool sortable = true;
  };

  class DataSource {
   public:
    virtual ~DataSource() = default;
    virtual int GetNumberOfRows() = 0;
    virtual std::string GetCellValue(int row_index, int column_index) = 0;
    virtual void SortByColumn(int column_index, bool ascending) = 0;
    virtual CellHighlightability GetCellHighlightablity(int row_index,
                                                        int column_index) {
      return CellHighlightability::NotHighlightable;
    }
  };

  template <typename... Modifiers>
  static std::shared_ptr<Node> BasicTable(
      std::shared_ptr<DataSource> data_source,
      const std::vector<Column>& columns, Modifiers... modifiers) {
    std::shared_ptr<Node> header_container;
    std::shared_ptr<Node> rows_container;

    auto node = Container::VerticalContainer(
        [&header_container, &rows_container, data_source,
         columns](Table& table) {
          table.Initialize(data_source, columns, header_container,
                           rows_container);
        },
        [](Block& block) {
          block.SetFillColor(kTableBackgroundColor);
          block.SetBorderColor(kTableBorderColor);
          block.SetBorderWidth(kTableBorderWidth);
          block.SetBorderRadius(kTableBorderRadius);
        },
        [](Layout& layout) {
          layout.SetFlexGrow(1.0f);
          layout.SetFlexShrink(1.0f);
          layout.SetMinHeight(0.0f);
          layout.SetGap(0.0f);
        },
        // Header row
        Container::HorizontalContainer(
            [](Block& block) {
              block.SetFillColor(kTableHeaderBackgroundColor);
            },
            [](Layout& layout) {
              layout.SetWidthPercent(100.0f);
              layout.SetPadding(YGEdgeVertical, kTableHeaderVerticalPadding);
              layout.SetGap(0.0f);
            },
            Node::Empty(
                [](Layout& layout) {
                  layout.SetFlexDirection(YGFlexDirectionRow);
                  layout.SetFlexGrow(1.0f);
                  layout.SetMargin(YGEdgeRight, kWidgetSpacing);
                },
                &header_container),
            Node::Empty(
                [](Layout& layout) { layout.SetWidth(ScrollBar::kWidth); })),
        // Divider
        Container::HorizontalContainer(
            [](Block& block) { block.SetFillColor(kTableDividerColor); },
            [](Layout& layout) {
              layout.SetWidthPercent(100.0f);
              layout.SetHeight(kTableDividerHeight);
            }),
        // Scrollable body
        ScrollContainer::VerticalScrollContainer(
            Node::Empty(
                [](Layout& layout) {
                  layout.SetFlexDirection(YGFlexDirectionColumn);
                  layout.SetWidthPercent(100.0f);
                },
                &rows_container),
            [](Block& block) { block.SetFillColor(kTableBackgroundColor); },
            [](Layout& layout) {
              layout.SetFlexGrow(1.0f);
              layout.SetFlexShrink(1.0f);
              layout.SetMinHeight(0.0f);
              layout.SetWidthPercent(100.0f);
              layout.SetGap(0.0f);
            }));

    node->Apply(modifiers...);
    return node;
  }

  Table();

  void SetNode(std::weak_ptr<Node> node);

  // Refreshes/rebuilds the table content from the data source.
  void Refresh();

  void OnCellSelect(std::function<void(int, int)> on_cell_select);
  void OnCellHover(std::function<void(int, int)> on_cell_hover);

 private:
  std::weak_ptr<Node> node_;
  std::shared_ptr<DataSource> data_source_;
  std::vector<Column> columns_;

  std::weak_ptr<Node> header_container_;
  std::weak_ptr<Node> rows_container_;

  std::vector<std::shared_ptr<Node>> row_nodes_;
  std::vector<std::vector<std::shared_ptr<Node>>> cell_nodes_;

  int sorted_column_index_ = -1;
  bool sort_ascending_ = true;

  int hovered_row_index_ = -1;
  int hovered_col_index_ = -1;

  std::vector<std::function<void(int, int)>> on_cell_select_;
  std::vector<std::function<void(int, int)>> on_cell_hover_;

  void Initialize(std::shared_ptr<DataSource> data_source,
                  const std::vector<Column>& columns,
                  std::shared_ptr<Node> header_container,
                  std::shared_ptr<Node> rows_container);
  void BuildTable();
  void Sort(int column_index);
  void HoverCell(int row, int col);
  void LeaveCell(int row, int col);
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::Table>;

}  // namespace perception
