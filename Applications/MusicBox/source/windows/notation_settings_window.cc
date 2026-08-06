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

#include "windows/notation_settings_window.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "perception/ui/components/container.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/slider.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"

using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Container;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::Slider;
using ::perception::ui::components::UiWindow;

namespace windows {

namespace {

// Available beat division denominator values corresponding to slider indices.
constexpr int kDividers[] = {1, 2, 4, 8, 16, 32};

// Human-readable display names for each slider beat division option.
constexpr const char* kNames[] = {"1", "1/2", "1/4", "1/8", "1/16", "1/32"};

int DividerToSliderIndex(int divider) {
  divider = SanitizeNotePerBeat(divider);
  switch (divider) {
    case 1:
      return 0;
    case 2:
      return 1;
    case 4:
      return 2;
    case 8:
      return 3;
    case 16:
      return 4;
    case 32:
      return 5;
    default:
      return 2;
  }
}

int SliderIndexToDivider(int index) {
  index = std::clamp(index, 0, 5);
  return kDividers[index];
}

const char* SliderIndexToName(int index) {
  index = std::clamp(index, 0, 5);
  return kNames[index];
}

}  // namespace

NotationSettingsWindow::NotationSettingsWindow(
    SongMetadata& metadata, std::function<void()> on_beats_per_bar_changed,
    std::function<void()> on_note_per_beat_changed,
    std::function<void()> on_closed)
    : metadata_(metadata),
      on_beats_per_bar_changed_(std::move(on_beats_per_bar_changed)),
      on_note_per_beat_changed_(std::move(on_note_per_beat_changed)),
      on_closed_(std::move(on_closed)) {
  BuildUI();
}

void NotationSettingsWindow::Focus() {
  if (window_node_) {
    if (auto ui_win = window_node_->Get<UiWindow>()) {
      ui_win->Focus();
    }
  }
}

void NotationSettingsWindow::Close() {
  if (window_node_) {
    if (auto ui_win = window_node_->Get<UiWindow>()) {
      ui_win->Close();
    }
  }
}

void NotationSettingsWindow::UpdateValues(const SongMetadata& metadata) {
  metadata_.beats_per_bar = metadata.beats_per_bar;
  metadata_.note_per_beat = SanitizeNotePerBeat(metadata.note_per_beat);

  if (beats_per_bar_input_node_) {
    if (auto input_box = beats_per_bar_input_node_->Get<InputBox>()) {
      input_box->SetText(std::to_string(metadata_.beats_per_bar));
    }
  }

  int idx = DividerToSliderIndex(metadata_.note_per_beat);
  if (note_per_beat_label_node_) {
    if (auto label = note_per_beat_label_node_->Get<Label>()) {
      label->SetText(SliderIndexToName(idx));
    }
  }
  if (note_per_beat_slider_node_) {
    if (auto slider = note_per_beat_slider_node_->Get<Slider>()) {
      slider->SetValue(static_cast<float>(idx));
    }
  }
}

void NotationSettingsWindow::BuildUI() {
  int initial_slider_idx = DividerToSliderIndex(metadata_.note_per_beat);

  auto content = Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetGap(16.0f);
        layout.SetPadding(YGEdgeAll, 16.0f);
      },

      // Beats per bar
      Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetAlignItems(YGAlignCenter);
            layout.SetGap(8.0f);
          },
          Label::BasicLabel("Beats per bar:"),
          InputBox::BasicInputBox(
              std::to_string(metadata_.beats_per_bar),
              [this](InputBox& input_box) {
                input_box.OnTextChanged([this](std::string_view text) {
                  try {
                    int beats_per_bar = std::stoi(std::string(text));
                    if (beats_per_bar > 0 &&
                        beats_per_bar != metadata_.beats_per_bar) {
                      metadata_.beats_per_bar = beats_per_bar;
                      if (on_beats_per_bar_changed_)
                        on_beats_per_bar_changed_();
                    }
                  } catch (...) {
                  }
                });
              },
              [](Layout& layout) { layout.SetWidth(60.0f); },
              &beats_per_bar_input_node_)),

      // Note per beat
      Container::VerticalContainer(
          [](Layout& layout) { layout.SetGap(6.0f); },
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetAlignItems(YGAlignCenter);
                layout.SetGap(8.0f);
              },
              Label::BasicLabel("Note per beat:"),
              Label::BasicLabel(SliderIndexToName(initial_slider_idx),
                                &note_per_beat_label_node_)),
          Slider::BasicSlider(
              0.0f, 5.0f, static_cast<float>(initial_slider_idx),
              [this](float value) {
                int idx = std::clamp(static_cast<int>(std::round(value)), 0, 5);
                if (note_per_beat_label_node_) {
                  if (auto label = note_per_beat_label_node_->Get<Label>()) {
                    label->SetText(SliderIndexToName(idx));
                  }
                }
                int new_divider = SliderIndexToDivider(idx);
                if (new_divider != metadata_.note_per_beat) {
                  metadata_.note_per_beat = new_divider;
                  if (on_note_per_beat_changed_) on_note_per_beat_changed_();
                }
              },
              [](Layout& layout) { layout.SetWidth(220.0f); },
              &note_per_beat_slider_node_)));

  if (window_node_) {
    window_node_->RemoveChildren();
    window_node_->AddChild(content);
  } else {
    window_node_ = UiWindow::ResizableWindowWithTitleBar(
        "Notation Settings",
        [this](UiWindow& window) {
          window.OnClose([this]() {
            window_node_.reset();
            if (on_closed_) on_closed_();
          });
        },
        [](Layout& layout) {
          layout.SetWidth(320.0f);
          layout.SetHeight(180.0f);
        },
        content);
  }
}

}  // namespace windows
