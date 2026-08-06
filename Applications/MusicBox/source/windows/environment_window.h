// Copyright 2026 Google LLC
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
#include <memory>

#include "perception/ui/node.h"

namespace windows {

class EnvironmentWindow {
 public:
  EnvironmentWindow(std::function<void()> on_changed,
                    std::function<void()> on_closed);
  ~EnvironmentWindow() = default;

  void Focus();
  void Close();

  std::shared_ptr<perception::ui::Node> GetNode() const { return window_node_; }

 private:
  void BuildUI();

  std::function<void()> on_changed_;
  std::function<void()> on_closed_;

  std::shared_ptr<perception::ui::Node> window_node_;
  std::shared_ptr<perception::ui::Node> content_node_;
};

}  // namespace windows
