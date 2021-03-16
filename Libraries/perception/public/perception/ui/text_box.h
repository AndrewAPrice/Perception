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

#include "perception/ui/text_alignment.h"
#include "perception/ui/widget.h"

#include <functional>
#include <string_view>
#include <string>

namespace perception {
namespace ui {

struct DrawContext;

class TextBox : public Widget {
public:
	TextBox();
	virtual ~TextBox();

	TextBox* SetValue(std::string_view value);
	std::string_view GetValue();
	TextBox* SetPadding(int padding);
	TextBox* SetTextAlignment(TextAlignment alignment);
	TextBox* SetEditable(bool editable);
	bool IsEditable();
	TextBox* OnChange(std::function<void()> on_change_handler);

private:
    virtual void Draw(DrawContext& draw_context) override;

	virtual int CalculateContentWidth() override;
    virtual int CalculateContentHeight() override;

    virtual void OnNewWidth(int width) override;
    virtual void OnNewHeight(int height) override;
    
	std::string value_;
	int padding_;
	bool is_editable_;
	std::function<void()> on_change_handler_;
	TextAlignment text_alignment_;
	bool realign_text_;
	int text_x_, text_y_;
};

}
}
