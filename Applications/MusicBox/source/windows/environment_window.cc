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

#include "perception/scheduler.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/checkbox.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/slider.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"
#include "synth_engine.h"

using ::perception::Defer;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Button;
using ::perception::ui::components::Checkbox;
using ::perception::ui::components::Container;
using ::perception::ui::components::Label;
using ::perception::ui::components::Slider;
using ::perception::ui::components::UiWindow;

namespace {

// Width of the environment slider controls in pixels.
constexpr float kEnvironmentSliderWidth = 180.0f;

// Preset parameters for Studio Room.
constexpr float kStudioRoomSize = 0.35f;
// Damping factor for Studio Room.
constexpr float kStudioRoomDamping = 0.20f;
// Wet/dry mix for Studio Room.
constexpr float kStudioRoomMix = 0.15f;

// Preset parameters for Concert Hall.
constexpr float kConcertHallSize = 0.75f;
// Damping factor for Concert Hall.
constexpr float kConcertHallDamping = 0.40f;
// Wet/dry mix for Concert Hall.
constexpr float kConcertHallMix = 0.25f;

// Preset parameters for Cathedral.
constexpr float kCathedralSize = 0.95f;
// Damping factor for Cathedral.
constexpr float kCathedralDamping = 0.50f;
// Wet/dry mix for Cathedral.
constexpr float kCathedralMix = 0.40f;

}  // namespace

namespace windows {

EnvironmentWindow::EnvironmentWindow(std::function<void()> on_changed,
                                     std::function<void()> on_closed)
    : on_changed_(std::move(on_changed)), on_closed_(std::move(on_closed)) {
  BuildUI();
}

void EnvironmentWindow::Focus() {
  if (window_node_) {
    if (auto ui_win = window_node_->Get<UiWindow>())
      ui_win->Focus();
  }
}

void EnvironmentWindow::Close() {
  if (window_node_) {
    if (auto ui_win = window_node_->Get<UiWindow>())
      ui_win->Close();
  }
}

void EnvironmentWindow::BuildUI() {
  auto content = Container::VerticalContainer(
      [](Layout& layout) { layout.SetWidthPercent(100.0f); },

      // Enable Reverb Checkbox
      Checkbox::BasicCheckbox("Enable Acoustic Reverb", IsReverbEnabled(),
                              [this](bool checked) {
                                SetReverbEnabled(checked);
                                if (on_changed_) on_changed_();
                              }),

      Label::BasicLabel("Acoustic Environment Presets:"),

      // Preset Buttons
      Container::HorizontalContainer(
          Button::TextButton("Studio Room",
                             [this]() {
                               SetReverbEnabled(true);
                               SetReverbRoomSize(kStudioRoomSize);
                               SetReverbDamping(kStudioRoomDamping);
                               SetReverbMix(kStudioRoomMix);
                               if (on_changed_) on_changed_();
                               Defer([this]() { BuildUI(); });
                             }),
          Button::TextButton("Concert Hall",
                             [this]() {
                               SetReverbEnabled(true);
                               SetReverbRoomSize(kConcertHallSize);
                               SetReverbDamping(kConcertHallDamping);
                               SetReverbMix(kConcertHallMix);
                               if (on_changed_) on_changed_();
                               Defer([this]() { BuildUI(); });
                             }),
          Button::TextButton("Cathedral",
                             [this]() {
                               SetReverbEnabled(true);
                               SetReverbRoomSize(kCathedralSize);
                               SetReverbDamping(kCathedralDamping);
                               SetReverbMix(kCathedralMix);
                               if (on_changed_) on_changed_();
                               Defer([this]() { BuildUI(); });
                             })),

      // Manual Sliders
      Container::HorizontalContainer(
          [](Layout& layout) { layout.SetAlignItems(YGAlignCenter); },
          Label::BasicLabel("Room Size:"),
          Slider::BasicSlider(
              0.0f, 1.0f, GetReverbRoomSize(),
              [this](float val) {
                SetReverbRoomSize(val);
                if (on_changed_) on_changed_();
              },
              [](Layout& layout) {
                layout.SetWidth(kEnvironmentSliderWidth);
              })),

      Container::HorizontalContainer(
          [](Layout& layout) { layout.SetAlignItems(YGAlignCenter); },
          Label::BasicLabel("Reverb Mix:"),
          Slider::BasicSlider(
              0.0f, 1.0f, GetReverbMix(),
              [this](float val) {
                SetReverbMix(val);
                if (on_changed_) on_changed_();
              },
              [](Layout& layout) {
                layout.SetWidth(kEnvironmentSliderWidth);
              })));

  if (content_node_) {
    content_node_->RemoveChildren();
    content_node_->AddChild(content);
    content_node_->Invalidate();
  } else {
    content_node_ = Container::VerticalContainer(
        [](Layout& layout) { layout.SetWidthPercent(100.0f); }, content);
    window_node_ = UiWindow::DialogWithTitleBar(
        "Environment",
        [this](UiWindow& window) {
          window.OnClose([this]() {
            window_node_.reset();
            content_node_.reset();
            if (on_closed_) on_closed_();
          });
        },
        content_node_);
  }
}

}  // namespace windows
