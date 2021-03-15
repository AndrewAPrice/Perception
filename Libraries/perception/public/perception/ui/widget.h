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

#include <memory>

namespace perception {
namespace ui {

constexpr int kFillParent = -1;
constexpr int kFitContent = -2;

class DrawContext;

class Widget : public std::enable_shared_from_this<Widget> {
public:
	Widget();
	~Widget();
    std::shared_ptr<Widget> ToSharedPtr() {
        return shared_from_this();
    }

	Widget* SetWidth(int width);
	Widget* SetHeight(int height);
    Widget* SetSize(int width_and_height);
    Widget* SetSize(int width, int height);

	int GetWidth();
	int GetHeight();
	int GetCalculatedWidth();
	int GetCalculatedHeight();
	void VerifyCalculatedSize();

	std::weak_ptr<Widget> GetParent();

    // The below functions are not intended for end users unless
    // you are building widgets.
    virtual void Draw(DrawContext& draw_context) = 0;
    void SetParent(std::weak_ptr<Widget> parent);
    void ClearParent();

    void SetCalculatedWidth(int width);
    void SetCalculatedHeight(int height);
    void RecalculateWidth();
    void RecalculateHeight();
    void InvalidateSize();

    virtual void InvalidateCalculatedWidth();
    virtual void InvalidateCalculatedHeight();

protected:

    // Components that have children or some other calculation should
    // override these.
    virtual void OnNewHeight(int height);
    virtual void OnNewWidth(int width);

    // Components that have children should override these.
    virtual void InvalidateChildrensCalculatedWidth();
    virtual void InvalidateChildrensCalculatedHeight();

    // Components should override these two functions to determine how
    // big they should be if their dimensions are FIT_CONTENT.
    virtual int CalculateContentWidth();
    virtual int CalculateContentHeight();

    virtual void InvalidateRender();

	std::weak_ptr<Widget> parent_;
    int width_;
    int height_;
    bool calculated_width_invalidated_;
    bool calculated_height_invalidated_;
    int calculated_width_;
    int calculated_height_;
};

}
}