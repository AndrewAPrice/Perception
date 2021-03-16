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

#include "perception/ui/widget.h"

namespace perception {
namespace ui {

Widget::Widget() :
	width_(kFillParent), height_(kFillParent),
	calculated_width_invalidated_(true),
	calculated_height_invalidated_(true) {}

Widget::~Widget() {}

Widget* Widget::SetWidth(int width) {
	if (width_ != width) {
		width_ = width;
		InvalidateCalculatedWidth();
	}
	return this;
}

Widget* Widget::SetHeight(int height) {
	if (height_ != height) {
		height_ = height;
		InvalidateCalculatedHeight();
	}
	return this;
}

Widget* Widget::SetSize(int width, int height) {
    return SetWidth(width)->SetHeight(height);
}

Widget* Widget::SetSize(int width_and_height) {
    return SetWidth(width_and_height)->
        SetHeight(width_and_height);
}

int Widget::GetWidth() {
	return width_;
}

int Widget::GetHeight() {
	return height_;
}

int Widget::GetCalculatedWidth() {
	if (calculated_height_invalidated_)
		RecalculateWidth();
	return calculated_width_;
}

int Widget::GetCalculatedHeight() {
	if (calculated_height_invalidated_)
		RecalculateHeight();
	return calculated_height_;
}

void Widget::VerifyCalculatedSize() {
    if (calculated_width_invalidated_) {
        RecalculateWidth();
    }
    if (calculated_height_invalidated_) {
        RecalculateHeight();
    }
}

std::weak_ptr<Widget> Widget::GetParent() {
	return parent_;
}

void Widget::SetParent(std::weak_ptr<Widget> parent) {
	parent_ = parent;
}

void Widget::ClearParent() {
	parent_.reset();
}

void Widget::SetCalculatedWidth(int width) {
    if (calculated_width_ != width) {
        calculated_width_ = width;
        OnNewWidth(width);
    }
    calculated_width_invalidated_ = false;
}

void Widget::SetCalculatedHeight(int height)  {
    if (calculated_height_ != height) {
        calculated_height_ = height;
        OnNewHeight(height);
    }
    calculated_height_invalidated_ = false;
}

void Widget::RecalculateWidth() {
    if (width_ >= 0) {
        SetCalculatedWidth(width_);
    } else if (width_ == kFillParent) {
        if (auto parent = parent_.lock()) {
            parent->RecalculateWidth();
        } else {
            // No parent.
            SetCalculatedWidth(0);
        }
    } else if (width_ == kFitContent) {
        SetCalculatedWidth(CalculateContentWidth());
    } else {
        SetCalculatedWidth(0);
    }
}

void Widget::RecalculateHeight() {
    if (height_ >= 0) {
        SetCalculatedHeight(height_);
    } else if (height_ == kFillParent) {
        if (auto parent = parent_.lock()) {
            parent->RecalculateHeight();
        } else {
            // No parent.
            SetCalculatedHeight(0);
        }
    } else if (height_ == kFitContent) {
        SetCalculatedHeight(CalculateContentHeight());
    } else {
        SetCalculatedHeight(0);
    }
}

void Widget::InvalidateSize() {
    InvalidateCalculatedWidth();
    InvalidateCalculatedHeight();
}

void Widget::InvalidateCalculatedWidth() {
    if (calculated_width_invalidated_) {
        return;
    }
    calculated_width_invalidated_ = true;

    if (auto parent = parent_.lock()) {
        if (parent->GetWidth() == kFitContent) {
            parent->InvalidateCalculatedWidth();
        }
    }
    InvalidateChildrensCalculatedWidth();
}

void Widget::InvalidateCalculatedHeight() {
    if (calculated_height_invalidated_) {
        return;
    }
    calculated_height_invalidated_ = true;

    if (auto parent = parent_.lock()) {
        if (parent->GetHeight() == kFitContent) {
            parent->InvalidateCalculatedWidth();
        }
    }
    InvalidateChildrensCalculatedHeight();
}

bool Widget::GetWidgetAt(int x, int y,
    std::shared_ptr<Widget>& widget,
    int& x_in_selected_widget,
    int& y_in_selected_widget) {
    if (x >= 0 && x < GetCalculatedWidth() &&
        y >= 0 && y < GetCalculatedHeight()) {
        // Within our bounds, but not selectable.
        widget.reset();
        return true;
    } else {
        // Outside our bounds.
        return false;
    }
}

// Components that have children or some other calculation should
// override these.
void Widget::OnNewHeight(int height) {}
void Widget::OnNewWidth(int width) {}

// Components that have children should override these.
void Widget::InvalidateChildrensCalculatedWidth() {}
void Widget::InvalidateChildrensCalculatedHeight() {}

// Components should override these two functions to determine how
// big they should be if their dimensions are kFitContent.
int Widget::CalculateContentWidth() {
	return 0;
}

int Widget::CalculateContentHeight() {
	return 0;
}

void Widget::OnMouseEnter() { }
void Widget::OnMouseLeave() {}
void Widget::OnMouseMove(int x, int y) {}
void Widget::OnMouseButtonDown(int x, int y,
    ::permebuf::perception::devices::MouseButton button) {}
void Widget::OnMouseButtonUp(int x, int y,
    ::permebuf::perception::devices::MouseButton button) {}

void Widget::InvalidateRender() {
    if (auto parent = parent_.lock()) {
       parent->InvalidateRender();
    }
}

}
}
