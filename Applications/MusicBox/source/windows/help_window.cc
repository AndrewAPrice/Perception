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

#include "windows/help_window.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "perception/ui/components/markdown.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"

using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Markdown;
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::UiWindow;

namespace windows {
namespace {

// Default width for the Help window.
constexpr float kHelpWindowWidth = 560.0f;

// Default height for the Help window.
constexpr float kHelpWindowHeight = 480.0f;

// Path of the help markdown file.
constexpr const char* kHelpMarkdownPath = "/Applications/MusicBox/help.md";

// Loads the help markdown text from the file.
std::string LoadHelpMarkdownFromDisk() {
  std::error_code ec;
  if (std::filesystem::exists(kHelpMarkdownPath, ec) &&
      std::filesystem::is_regular_file(kHelpMarkdownPath, ec)) {
    std::ifstream file(kHelpMarkdownPath, std::ios::in | std::ios::binary);
    if (file.is_open()) {
      std::string content((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
      if (!content.empty()) return content;
    }
  }
  return "";
}

}  // namespace

HelpWindow::HelpWindow(std::function<void()> on_closed)
    : on_closed_(std::move(on_closed)) {
  BuildUI();
}

void HelpWindow::Focus() {
  if (!window_node_) return;
  if (auto ui_win = window_node_->Get<UiWindow>()) ui_win->Focus();
}

void HelpWindow::Close() {
  if (!window_node_) return;
  if (auto ui_win = window_node_->Get<UiWindow>()) ui_win->Close();
}

void HelpWindow::BuildUI() {
  std::string markdown_text = LoadHelpMarkdownFromDisk();

  auto content = Markdown::GenerateNode(
      markdown_text, [](Layout& layout) { layout.SetWidthPercent(100.0f); });

  auto scroll_container = ScrollContainer::VerticalScrollContainer(content);

  window_node_ = UiWindow::ResizableWindowWithTitleBar(
      "Welcome to MusicBox",
      [this](UiWindow& window) {
        window.OnClose([this]() {
          window_node_.reset();
          if (on_closed_) on_closed_();
        });
      },
      [](Layout& layout) {
        layout.SetWidth(kHelpWindowWidth);
        layout.SetHeight(kHelpWindowHeight);
      },
      scroll_container);
}

}  // namespace windows
