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

#include "perception/ui/widget.h"

#include <list>
#include <memory>
#include <string_view>
#include <vector>

namespace perception {
namespace ui {

class DrawContext;
class UIWindow;

// Note, FixedGrid's size can't be kFitContent.
class FixedGrid : public Widget {
public:
	FixedGrid();
	virtual ~FixedGrid();

    FixedGrid* AddChildren(const std::vector<std::shared_ptr<Widget>>& children);
	FixedGrid* AddChild(std::shared_ptr<Widget> child,
        int x = -1, int y = -1, int columns = 1, int rows = 1);
    FixedGrid* SetColumns(int columns);
    FixedGrid* SetRows(int rows);
	FixedGrid* SetSpacing(int spacing);
    FixedGrid* SetMargin(int margin);

    virtual bool GetWidgetAt(int x, int y,
        std::shared_ptr<Widget>& widget,
        int& x_in_selected_widget,
        int& y_in_selected_widget) override;
protected:
    struct FixedGridItem {
        int x, y, columns, rows;
        std::shared_ptr<Widget> widget;
    };

    void FindEmptyPosition(int& x, int& y, int columns, int rows);
    bool IsValidEmptyPosition(int x, int y, int columns, int rows);

    virtual void Draw(DrawContext& draw_context) override;

    virtual void OnNewWidth(int width) override;
    virtual void OnNewHeight(int height) override;

    virtual int CalculateContentWidth() override;
    virtual int CalculateContentHeight() override;

    virtual void InvalidateChildrensCalculatedWidth() override;
    virtual void InvalidateChildrensCalculatedHeight() override;

    int rows_;
    int columns_;
    int spacing_;
    int margin_;
    int cell_width_;
    int cell_height_;
    int x_spacing_;
    int y_spacing_;
    std::list<FixedGridItem> children_;
};

}
}
