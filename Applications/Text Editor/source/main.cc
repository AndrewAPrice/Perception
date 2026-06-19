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

#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "include/core/SkColor.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/open_file_dialog.h"
#include "perception/ui/components/text_field.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"

using ::perception::HandOverControl;
using ::perception::TerminateProcess;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::Label;
using ::perception::ui::components::ShowOpenFileDialog;
using ::perception::ui::components::TextField;
using ::perception::ui::components::UiWindow;

namespace {

int opened_instances = 0;
std::vector<std::shared_ptr<Node>> open_windows;

struct EditorState {
  std::weak_ptr<Node> text_field_node;
  std::weak_ptr<Node> window_node;
};

void ShowErrorDialog(std::string_view title, std::string_view message) {
  opened_instances++;
  auto window = UiWindow::DialogWithTitleBar(
      title,
      [](Layout& layout) {
        layout.SetWidth(320.0f);
        layout.SetHeight(140.0f);
      },
      [](UiWindow& window) {
        window.OnClose([]() {
          opened_instances--;
          if (opened_instances == 0) TerminateProcess();
        });
      },
      Label::BasicLabel(message, [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetJustifyContent(YGJustifyCenter);
        layout.SetAlignContent(YGAlignCenter);
      }));
  open_windows.push_back(window);
}

void OpenEditor(std::string_view initial_path) {
  opened_instances++;

  std::string file_path(initial_path);
  std::string initial_content = "";

  if (!file_path.empty()) {
    FILE* fp = fopen(file_path.c_str(), "rb");
    if (fp) {
      fseek(fp, 0, SEEK_END);
      long file_size = ftell(fp);
      fseek(fp, 0, SEEK_SET);
      if (file_size > 0) {
        std::string buffer(file_size, '\0');
        fread(buffer.data(), 1, file_size, fp);
        initial_content = buffer;
      }
      fclose(fp);
    } else {
      ShowErrorDialog("Error Opening File", "Could not open requested file.");
    }
  }

  std::string window_title =
      file_path.empty() ? "Text Editor" : "Text Editor - " + file_path;

  auto state = std::make_shared<EditorState>();

  auto window = UiWindow::ResizableWindowWithTitleBar(
      window_title,
      [](Layout& layout) {
        layout.SetWidth(500.0f);
        layout.SetHeight(400.0f);
      },
      [](UiWindow& window) {
        window.OnClose([]() {
          opened_instances--;
          if (opened_instances == 0) TerminateProcess();
        });
      },
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
            layout.SetMinHeight(0.0f);
            layout.SetMinWidth(0.0f);
          },
          // Toolbar
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetWidthPercent(100.0f);
                layout.SetAlignItems(YGAlignCenter);
              },
              Button::TextButton(
                  "New",
                  [state]() {
                    if (auto text_field_node = state->text_field_node.lock())
                      text_field_node->Get<TextField>()->SetText("");
                    if (auto window_node = state->window_node.lock())
                      window_node->Get<UiWindow>()->SetTitle("Text Editor");
                  }),
              Button::TextButton(
                  "Open",
                  [state]() {
                    ShowOpenFileDialog(
                        [state](bool succeeded, std::string_view path) {
                          if (succeeded && !path.empty()) {
                            FILE* fp = fopen(std::string(path).c_str(), "rb");
                            if (fp) {
                              fseek(fp, 0, SEEK_END);
                              long file_size = ftell(fp);
                              fseek(fp, 0, SEEK_SET);
                              std::string buffer(file_size, '\0');
                              if (file_size > 0) {
                                fread(buffer.data(), 1, file_size, fp);
                              }
                              fclose(fp);
                              if (auto text_field_node =
                                      state->text_field_node.lock()) {
                                text_field_node->Get<TextField>()->SetText(
                                    buffer);
                              }
                              if (auto window_node =
                                      state->window_node.lock()) {
                                window_node->Get<UiWindow>()->SetTitle(
                                    "Text Editor - " + std::string(path));
                              }
                            }
                          }
                        },
                        {".txt"}, "", "Open Text File");
                  })),
          // Text Field Area
          TextField::BasicTextField(
              initial_content,
              [](Layout& layout) {
                layout.SetFlexGrow(1.0f);
                layout.SetFlexShrink(1.0f);
                layout.SetMinHeight(0.0f);
                layout.SetMinWidth(0.0f);
              },
              &state->text_field_node)),
      &state->window_node);

  open_windows.push_back(window);
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc >= 2) {
    OpenEditor(argv[1]);
  } else {
    OpenEditor("");
  }

  HandOverControl();
  return 0;
}
