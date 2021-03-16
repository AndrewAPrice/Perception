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

class VerticalContainer : public Widget {
public:
	VerticalContainer();
	virtual ~VerticalContainer();

    VerticalContainer* AddChildren(const std::vector<std::shared_ptr<Widget>>& children);
	VerticalContainer* AddChild(std::shared_ptr<Widget> child);
	VerticalContainer* RemoveChild(std::shared_ptr<Widget> child);
	VerticalContainer* SetMargin(int margin);
	VerticalContainer* SetSpacing(int spacing);

    virtual bool GetWidgetAt(int x, int y,
        std::shared_ptr<Widget>& widget,
        int& x_in_selected_widget,
        int& y_in_selected_widget) override;
protected:
    virtual void Draw(DrawContext& draw_context) override;

    virtual void OnNewWidth(int width) override;
    virtual void OnNewHeight(int height) override;

    virtual int CalculateContentWidth() override;
    virtual int CalculateContentHeight() override;

    virtual void InvalidateChildrensCalculatedWidth() override;
    virtual void InvalidateChildrensCalculatedHeight() override;

    int margin_;
    int spacing_;
    std::list<std::shared_ptr<Widget>> children_;

};

}
}
