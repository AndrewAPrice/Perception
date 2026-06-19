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

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/time.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/image_view.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/open_file_dialog.h"
#include "perception/ui/components/slider.h"
#include "perception/ui/components/text_field.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/font.h"
#include "perception/ui/image.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/resize_method.h"
#include "perception/ui/text_alignment.h"

using ::perception::HandOverControl;
using ::perception::SleepForDuration;
using ::perception::TerminateProcess;
using ::perception::ui::Layout;
using ::perception::ui::LoadFont;
using ::perception::ui::Node;
using ::perception::ui::ResizeMethod;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Container;
using ::perception::ui::components::ImageView;
using ::perception::ui::components::Label;
using ::perception::ui::components::Slider;
using ::perception::ui::components::TextField;
using ::perception::ui::components::UiWindow;

namespace {

int opened_instances = 0;
std::vector<std::shared_ptr<Node>> open_windows;

struct TtfViewerState {
  std::string path;
  std::weak_ptr<Node> text_field_node;
  std::weak_ptr<Node> size_label_node;
};

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

void OpenTtf(std::string_view path) {
  SkFont* initial_font = LoadFont(path, 24.0f);
  if (!initial_font) {
    ShowErrorDialog("Unsupported Font",
                    std::string("The file \"") + std::string(path) +
                        "\" is not a valid ttf or supported font file.");
    return;
  }

  opened_instances++;

  auto state = std::make_shared<TtfViewerState>();
  state->path = std::string(path);

  auto window = UiWindow::ResizableWindowWithTitleBar(
      path,
      [](Layout& layout) {
        layout.SetWidth(600.0f);
        layout.SetHeight(400.0f);
      },
      [](UiWindow& window) {
        window.OnClose([]() {
          opened_instances--;
          if (opened_instances == 0) TerminateProcess();
        });
      },
      Container::HorizontalContainer(
          Label::BasicLabel(
              "Size: 24", [](Layout& layout) { layout.SetWidth(60.0f); },
              &state->size_label_node),
          Slider::BasicSlider(
              8.0f, 72.0f, 24.0f,
              [state](float new_size) {
                int size_int = static_cast<int>(std::round(new_size));
                if (auto label_node = state->size_label_node.lock()) {
                  label_node->Get<Label>()->SetText("Size: " +
                                                    std::to_string(size_int));
                }
                if (auto text_field_node = state->text_field_node.lock()) {
                  if (SkFont* font =
                          LoadFont(state->path, static_cast<float>(size_int))) {
                    text_field_node->Get<TextField>()->SetFont(font);
                  }
                }
              },
              [](Layout& layout) { layout.SetFlexGrow(1.0f); })),
      TextField::BasicTextField(
          "The quick brown fox jumps over the lazy dog.",
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
            layout.SetMinHeight(0.0f);
            layout.SetMinWidth(0.0f);
          },
          [initial_font](TextField& text_field) {
            text_field.SetFont(initial_font);
          },
          &state->text_field_node));

  open_windows.push_back(window);
}

bool IsTtfFile(std::string_view path) {
  std::string ext = std::filesystem::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext == ".ttf";
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    ::perception::ui::components::ShowOpenFileDialog(
        [](bool succeeded, std::string_view path) {
          if (succeeded) {
            if (IsTtfFile(path)) {
              OpenTtf(path);
            } else {
              OpenImage(path);
            }
            if (opened_instances == 0) {
              TerminateProcess();
            }
          } else {
            TerminateProcess();
          }
        },
        {".rgba", ".png", ".svg", ".bmp", ".jpg", ".jpeg", ".webp", ".gif",
         ".ico", ".wbmp", ".ttf"},
        "", "Open File");
  } else {
    std::string_view path = argv[1];
    if (IsTtfFile(path)) {
      OpenTtf(path);
    } else {
      OpenImage(path);
    }
  }

  HandOverControl();
  return 0;
}
