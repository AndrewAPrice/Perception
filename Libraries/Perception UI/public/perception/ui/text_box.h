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

#include <functional>
#include <string>
#include <string_view>

#include "perception/ui/label.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/widget.h"

namespace perception {
namespace ui {

struct DrawContext;

class TextBox : public Widget {
 public:
  static std::shared_ptr<TextBox> Create();
  virtual ~TextBox();

  TextBox* SetValue(std::string_view value);
  std::string_view GetValue();
  TextBox* SetTextAlignment(TextAlignment alignment);
  TextBox* SetEditable(bool editable);
  std::shared_ptr<Label> GetLabel();
  bool IsEditable();
  TextBox* OnChange(std::function<void()> on_change_handler);

 private:
  TextBox();
  virtual void Draw(DrawContext& draw_context) override;

  std::shared_ptr<Label> label_;
  bool is_editable_;
  std::function<void()> on_change_handler_;
  bool realign_text_;
  int text_x_, text_y_;
};

}  // namespace ui
}  // namespace perception
