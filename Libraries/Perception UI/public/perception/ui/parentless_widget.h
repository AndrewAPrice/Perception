// Copyright 2022 Google LLC
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

class ParentlessWidget : public Widget {
 public:
  static std::shared_ptr<ParentlessWidget> Create();
  virtual ~ParentlessWidget();

  void SetParentSize(float width, float height);
  void OnInvalidateParent(const std::function<void()>& invalidate_parent);
  virtual void InvalidateRender() override;

 private:
  ParentlessWidget();
  virtual void Draw(DrawContext& draw_context) override;
  void MaybeRecalculateLayout();

  bool invalidated_;
  float width_, height_;
  std::function<void()> invalidate_parent_;
};

}  // namespace ui
}  // namespace perception
