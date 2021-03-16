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

#include "perception/ui/fixed_grid.h"

#include "perception/ui/draw_context.h"

namespace perception {
namespace ui {

FixedGrid::FixedGrid() :
    rows_(2), columns_(2),
	spacing_ (8), margin_(0) {
}

FixedGrid::~FixedGrid() {}

FixedGrid* FixedGrid::AddChildren(
	const std::vector<std::shared_ptr<Widget>>& children) {
	for (auto child : children)
		AddChild(child);
    return this;
}

FixedGrid* FixedGrid::AddChild(std::shared_ptr<Widget> child,
    int x, int y, int columns, int rows) {
    if (x == -1 || y == -1) {
        // We need to find where we can fit this.
        FindEmptyPosition(x, y, columns, rows);
    }

    FixedGridItem item;
    item.x = x;
    item.y = y;
    item.columns = columns;
    item.rows = rows;
    item.widget = child;
	children_.push_back(item);
	child->SetParent(ToSharedPtr());
    InvalidateSize();
    return this;
}

FixedGrid* FixedGrid::SetColumns(int columns) {
    if (columns_ = columns)
        return this;

    columns_ = columns;
    InvalidateSize();
    return this;
}

FixedGrid* FixedGrid::SetRows(int rows) {
    if (rows_ = rows)
        return this;

    rows_ = rows;
    InvalidateSize();
    return this;
}

FixedGrid* FixedGrid::SetMargin(int margin) {
    if (margin_ == margin)
        return this;
    margin_ = margin;
    InvalidateSize();
    return this;
}

FixedGrid* FixedGrid::SetSpacing(int spacing) {
	if (spacing_ == spacing)
		return this;

	spacing_ = spacing;
	InvalidateSize();
    return this;
}

bool FixedGrid::GetWidgetAt(int x, int y,
        std::shared_ptr<Widget>& widget,
        int& x_in_selected_widget,
        int& y_in_selected_widget) {
    int width = GetCalculatedWidth();
    int height = GetCalculatedHeight();

    if (x < 0 || y < 0 || x >= width || y >= height) {
        // Out of bounds.
        return false;
    }

    // Remove the margins.
    x -= margin_;
    y -= margin_;

    for (auto& child : children_) {
        if (child.widget->GetWidgetAt(
            x - child.x * x_spacing_,
            y - child.y * y_spacing_,
            widget, x_in_selected_widget,
            y_in_selected_widget)) {
            // Widget in this child.
            return true;
        }
    }

    // Within bounds, but not over a selectable widget.
    widget.reset();
    return true;
}

void FixedGrid::Draw(DrawContext& draw_context) {
    VerifyCalculatedSize();

    int start_x = draw_context.x + margin_;
    int start_y = draw_context.y + margin_;

    for (const auto& child: children_) {
        draw_context.x = start_x + child.x * x_spacing_;
        draw_context.y = start_y + child.y * y_spacing_;

        child.widget->Draw(draw_context);
    }
}

void FixedGrid::OnNewWidth(int width) {
    width -= margin_ * 2 + spacing_ * (columns_ - 1);
    int column_width = width / columns_;
    x_spacing_ = column_width + spacing_;

    for (const auto& item: children_) {
        if (item.widget->GetWidth() == kFillParent) {
            item.widget->SetCalculatedWidth(
                column_width + x_spacing_ * (item.columns - 1));
        }
    }
}

void FixedGrid::OnNewHeight(int height) {
    height -= margin_ * 2 + spacing_ * (rows_ - 1);
    int row_height = height / rows_;
    y_spacing_ = row_height + spacing_;

    for (const auto& item : children_) {
        if (item.widget->GetHeight() == kFillParent) {
            item.widget->SetCalculatedHeight(
                row_height + y_spacing_ * (item.rows - 1));
        }
    }
}

void FixedGrid::FindEmptyPosition(int& x, int& y,
    int columns, int rows) {
    for (y = 0; true; y++) {
        for (x = 0; x < columns_; x++) {
            if (IsValidEmptyPosition(x, y, columns, rows))
                return;
        }
    }
}

bool FixedGrid::IsValidEmptyPosition(int x, int y,
    int columns, int rows) {
    // Make sure the X value is not out of bounds.
    if (x + columns > columns_)
        return false;

    // Check if we overlap any existing children.
    for (const auto& child : children_) {
        if (!(x + columns <= child.x ||
            x >= child.x + child.columns ||
            y + rows <= child.y ||
            y >= child.y + child.rows))
            // We overlap this child.
            return false;
    }

    // We are in bounds and don't overlap any times.
    return true;
}

int FixedGrid::CalculateContentWidth() {
    return 0;
}

int FixedGrid::CalculateContentHeight() {
    return 0;
}

void FixedGrid::InvalidateChildrensCalculatedWidth() {
    for (auto& child : children_) {
        if (child.widget->GetWidth() == kFillParent) {
            child.widget->InvalidateCalculatedWidth();
        }
    }
}

void FixedGrid::InvalidateChildrensCalculatedHeight() {
    for (auto& child : children_) {
        if (child.widget->GetWidth() == kFillParent) {
            child.widget->InvalidateCalculatedWidth();
        }
    }
}

}
}
