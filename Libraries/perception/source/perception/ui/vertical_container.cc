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

#include "perception/ui/vertical_container.h"

#include <cmath>

#include "perception/ui/draw_context.h"
#include "perception/ui/widget.h"

namespace perception {
namespace ui {

VerticalContainer::VerticalContainer() :
	spacing_ (8), margin_(0) {
}

VerticalContainer::~VerticalContainer() {}

VerticalContainer* VerticalContainer::AddChildren(
	const std::vector<std::shared_ptr<Widget>>& children) {
	for (auto child : children)
		AddChild(child);
	return this;
}

VerticalContainer* VerticalContainer::AddChild(std::shared_ptr<Widget> child) {
	children_.push_back(child);
	child->SetParent(ToSharedPtr());
    InvalidateSize();
    return this;
}

VerticalContainer* VerticalContainer::RemoveChild(std::shared_ptr<Widget> child) {
	child->ClearParent();
	children_.remove(child);
    InvalidateSize();
    return this;
}

VerticalContainer* VerticalContainer::SetMargin(int margin) {
	if (margin_ == margin)
		return this;

	margin_ = margin;
	InvalidateSize();
	return this;
}

VerticalContainer* VerticalContainer::SetSpacing(int spacing) {
	if (spacing_ == spacing)
		return this;

	spacing_ = spacing;
	InvalidateSize();
	return this;
}

bool VerticalContainer::GetWidgetAt(int x, int y,
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
        if (child->GetWidgetAt(x, y, widget,
            x_in_selected_widget, y_in_selected_widget)) {
            // Widget in this child.
            return true;
        }

        y -= child->GetCalculatedHeight() - spacing_;
    }

    // Within bounds, but not over a selectable widget.
    widget.reset();
    return true;
}

void VerticalContainer::Draw(DrawContext& draw_context) {
    VerifyCalculatedSize();

    int x = draw_context.x + margin_;
    int y = draw_context.y + margin_;

    for (auto& child : children_) {
        draw_context.x = x;
        draw_context.y = y;
        child->Draw(draw_context);
        y += + child->GetCalculatedHeight() + spacing_;
    }
}

void VerticalContainer::OnNewWidth(int width) {
    for (auto& child : children_) {
        if (child->GetWidth() == kFillParent) {
            child->SetCalculatedWidth(width - margin_ * 2);
        }
    }
}

void VerticalContainer::OnNewHeight(int height) {
    int fixed_item_height = -spacing_ + margin_ * 2;
    int fill_parent_children = 0;

    for (auto& child : children_) {
        fixed_item_height += spacing_;
        if (child->GetHeight() == kFillParent) {
            fill_parent_children++;
        } else {
            fixed_item_height += child->GetCalculatedHeight();
        }
    }

    if (fill_parent_children > 0) {
        int fill_space = height - fixed_item_height;
        int height_per_child = static_cast<double>(fill_space) /
                                    static_cast<double>(fill_parent_children);
        double pos = 0.0;
        int last_whole_number = 0;

        for (auto& child : children_) {
            if (child->GetHeight() == kFillParent) {
                pos += height_per_child;
                int next_whole_number = static_cast<int>(std::round(pos));
                child->SetCalculatedHeight(next_whole_number
                                            - last_whole_number);
                last_whole_number = next_whole_number;
            }
        }
    }

}

int VerticalContainer::CalculateContentWidth() {
    int widest_child = 0;

    for (auto& child : children_) {
        if (child->GetWidth() != kFillParent) {
            widest_child = std::max(widest_child,
                child->GetCalculatedWidth());
        }
    }

    return widest_child + margin_ * 2;
}

int VerticalContainer::CalculateContentHeight() {
    int total_height = -spacing_ + margin_ * 2;

    for (auto& child : children_) {
        total_height += spacing_;
        if (child->GetHeight() != kFillParent) {
            total_height += child->GetCalculatedHeight();
        }
    }
    return total_height;
}

void VerticalContainer::InvalidateChildrensCalculatedWidth() {
    for (auto& child : children_) {
        if (child->GetWidth() == kFillParent) {
            child->InvalidateCalculatedWidth();
        }
    }
}

void VerticalContainer::InvalidateChildrensCalculatedHeight() {
    for (auto& child : children_) {
        if (child->GetWidth() == kFillParent) {
            child->InvalidateCalculatedWidth();
        }
    }
}

}
}
