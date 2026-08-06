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

#include "windows/environment_window.h"

#include "perception/ui/components/button.h"
#include "perception/ui/components/checkbox.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/slider.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"
#include "synth_engine.h"

using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Button;
using ::perception::ui::components::Checkbox;
using ::perception::ui::components::Container;
using ::perception::ui::components::Label;
using ::perception::ui::components::Slider;
using ::perception::ui::components::UiWindow;

namespace windows {

EnvironmentWindow::EnvironmentWindow(std::function<void()> on_changed,
                                     std::function<void()> on_closed)
    : on_changed_(std::move(on_changed)), on_closed_(std::move(on_closed)) {
  BuildUI();
}

void EnvironmentWindow::Focus() {
  if (window_node_) {
    if (auto ui_win = window_node_->Get<UiWindow>()) {
      ui_win->Focus();
    }
  }
}

void EnvironmentWindow::Close() {
  if (window_node_) {
    if (auto ui_win = window_node_->Get<UiWindow>()) {
      ui_win->Close();
    }
  }
}

void EnvironmentWindow::BuildUI() {
  auto content = Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetGap(12.0f);
        layout.SetPadding(YGEdgeAll, 12.0f);
      },

      // Enable Reverb Checkbox
      Checkbox::BasicCheckbox("Enable Acoustic Reverb", IsReverbEnabled(),
                              [this](bool checked) {
                                SetReverbEnabled(checked);
                                if (on_changed_) on_changed_();
                              }),

      Label::BasicLabel("Acoustic Environment Presets:"),

      // Preset Buttons
      Container::HorizontalContainer(
          [](Layout& layout) { layout.SetGap(8.0f); },
          Button::TextButton("Studio Room",
                             [this]() {
                               SetReverbEnabled(true);
                               SetReverbRoomSize(0.35f);
                               SetReverbDamping(0.20f);
                               SetReverbMix(0.15f);
                               if (on_changed_) on_changed_();
                               BuildUI();
                             }),
          Button::TextButton("Concert Hall",
                             [this]() {
                               SetReverbEnabled(true);
                               SetReverbRoomSize(0.75f);
                               SetReverbDamping(0.40f);
                               SetReverbMix(0.25f);
                               if (on_changed_) on_changed_();
                               BuildUI();
                             }),
          Button::TextButton("Cathedral",
                             [this]() {
                               SetReverbEnabled(true);
                               SetReverbRoomSize(0.95f);
                               SetReverbDamping(0.50f);
                               SetReverbMix(0.40f);
                               if (on_changed_) on_changed_();
                               BuildUI();
                             })),

      // Manual Sliders
      Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetAlignItems(YGAlignCenter);
            layout.SetGap(8.0f);
          },
          Label::BasicLabel("Room Size:"),
          Slider::BasicSlider(
              0.0f, 1.0f, GetReverbRoomSize(),
              [this](float val) {
                SetReverbRoomSize(val);
                if (on_changed_) on_changed_();
              },
              [](Layout& layout) { layout.SetWidth(180.0f); })),

      Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetAlignItems(YGAlignCenter);
            layout.SetGap(8.0f);
          },
          Label::BasicLabel("Reverb Mix:"),
          Slider::BasicSlider(
              0.0f, 1.0f, GetReverbMix(),
              [this](float val) {
                SetReverbMix(val);
                if (on_changed_) on_changed_();
              },
              [](Layout& layout) { layout.SetWidth(180.0f); })));

  if (window_node_) {
    // If window already exists, update its content child
    window_node_->RemoveChildren();
    window_node_->AddChild(content);
  } else {
    window_node_ = UiWindow::ResizableWindowWithTitleBar(
        "Environment",
        [this](UiWindow& window) {
          window.OnClose([this]() {
            window_node_.reset();
            if (on_closed_) on_closed_();
          });
        },
        [](Layout& layout) { layout.SetWidth(380.0f); }, content);
  }
}

}  // namespace windows
