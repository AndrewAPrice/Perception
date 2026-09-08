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

#include "progress_dialog.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "perception/scheduler.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "perception/ui/theme.h"

using ::perception::Defer;
using ::perception::ui::GetBold12UiFont;
using ::perception::ui::kSecondaryTextColor;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Container;
using ::perception::ui::components::Label;
using ::perception::ui::components::UiWindow;

namespace dialogs {
namespace {

// Operation progress dialog width in pixels.
constexpr float kProgressDialogWidth = 360.0f;

// Active progress dialog instances.
std::vector<std::shared_ptr<Node>> active_dialogs;

}  // namespace

void CloseProgressDialog(std::shared_ptr<Node> progress_dialog) {
  if (!progress_dialog) return;

  if (auto ui_window = progress_dialog->Get<UiWindow>()) ui_window->Close();

  Defer([progress_dialog]() {
    auto it = std::find(active_dialogs.begin(), active_dialogs.end(),
                        progress_dialog);
    if (it != active_dialogs.end()) active_dialogs.erase(it);
  });
}

std::shared_ptr<Node> ShowProgressDialog(
    std::string_view title, std::string_view message,
    std::shared_ptr<Node> parent_window) {
  auto dialog_ptr = std::make_shared<std::shared_ptr<Node>>();

  *dialog_ptr = UiWindow::DialogWithTitleBar(
      title, UiWindow::Parent(parent_window),
      [dialog_ptr](UiWindow& window) {
        window.OnClose([dialog_ptr]() { CloseProgressDialog(*dialog_ptr); });
      },
      [](Layout& layout) { layout.SetWidth(kProgressDialogWidth); },
      Container::VerticalContainer(
          Label::BasicLabel(
              std::string(message),
              [](Label& label) { label.SetFont(GetBold12UiFont()); }),
          Label::BasicLabel("Please wait while the disk operation completes...",
                            [](Label& label) {
                              label.SetColor(kSecondaryTextColor);
                            })));

  active_dialogs.push_back(*dialog_ptr);
  return *dialog_ptr;
}

}  // namespace dialogs
