// Copyright 2023 Google LLC
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

#include <iostream>
#include <sstream>
#include <string_view>
#include <vector>

#include "include/core/SkColor.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/time.h"
#include "perception/ui/components/image_view.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/open_file_dialog.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/image.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/resize_method.h"
#include "perception/ui/text_alignment.h"

using ::perception::HandOverControl;
using ::perception::SleepForDuration;
using ::perception::TerminateProcess;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::ResizeMethod;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::ImageView;
using ::perception::ui::components::Label;
using ::perception::ui::components::UiWindow;

namespace {

int opened_instances = 0;
std::vector<std::shared_ptr<Node>> open_windows;

void ShowErrorDialog(std::string_view title, std::string_view message) {
  opened_instances++;
  auto window = UiWindow::DialogWithTitleBar(
      title,
      [](Layout& layout) {
        layout.SetWidth(320.0f);
        layout.SetHeight(140.0f);
        layout.SetPadding(YGEdgeAll, 16.0f);
      },
      [](UiWindow& window) {
        window.OnClose([]() {
          opened_instances--;
          if (opened_instances == 0) TerminateProcess();
        });
      },
      Label::BasicLabel(
          message,
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetJustifyContent(YGJustifyCenter);
            layout.SetAlignContent(YGAlignCenter);
          },
          [](Label& label) {
            label.SetTextAlignment(TextAlignment::MiddleCenter);
          }));
  open_windows.push_back(window);
}

void OpenImage(std::string_view path) {
  std::shared_ptr<::perception::ui::Image> image =
      ::perception::ui::Image::LoadImage(path);
  if (!image) {
    ShowErrorDialog(
        "Unsupported File",
        std::string("The file \"") + std::string(path) +
            "\" is not a valid svg, png, or other supported image file.");
    return;
  }

  opened_instances++;

  auto window = UiWindow::ResizableWindow(
      path,
      [](Layout& layout) {
        layout.SetJustifyContent(YGJustifyCenter);
        layout.SetAlignContent(YGAlignCenter);
      },
      [](UiWindow& window) {
        window.OnClose([]() {
          opened_instances--;
          if (opened_instances == 0) TerminateProcess();
        });
      },
      ImageView::BasicImage(image, [](ImageView& image_view) {
        image_view.SetAlignment(TextAlignment::MiddleCenter);
        image_view.SetResizeMethod(ResizeMethod::Contain);
      }));
  open_windows.push_back(window);
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    ::perception::ui::components::ShowOpenFileDialog(
        [](bool succeeded, std::string_view path) {
          if (succeeded) {
            OpenImage(path);
            if (opened_instances == 0) {
              TerminateProcess();
            }
          } else {
            TerminateProcess();
          }
        },
        {".rgba", ".png", "svg", ".bmp", ".jpg", ".jpeg"}, "", "Open Image");
  } else {
    OpenImage(argv[1]);
  }

  HandOverControl();
  return 0;
}
