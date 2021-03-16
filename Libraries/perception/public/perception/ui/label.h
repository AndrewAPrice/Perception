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

class Label : public Widget {
public:
	Label();
	virtual ~Label();

	Label* SetLabel(std::string_view label);
	std::string_view GetLabel();
	Label* SetPadding(int padding);
	Label* SetTextAlignment(TextAlignment alignment);

private:
    virtual void Draw(DrawContext& draw_context) override;

	virtual int CalculateContentWidth() override;
    virtual int CalculateContentHeight() override;

    virtual void OnNewWidth(int width) override;
    virtual void OnNewHeight(int height) override;
    
	std::string label_;
	int padding_;
	TextAlignment text_alignment_;
	bool realign_text_;
	int text_x_, text_y_;
};

}
}
