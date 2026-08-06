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
#include "song_serializer.h"

namespace windows {

class NotationSettingsWindow {
 public:
  NotationSettingsWindow(SongMetadata& metadata,
                         std::function<void()> on_beats_per_bar_changed,
                         std::function<void()> on_note_per_beat_changed,
                         std::function<void()> on_closed);
  ~NotationSettingsWindow() = default;

  void Focus();
  void Close();
  void UpdateValues(const SongMetadata& metadata);

  std::shared_ptr<perception::ui::Node> GetNode() const { return window_node_; }

 private:
  void BuildUI();

  SongMetadata& metadata_;
  std::function<void()> on_beats_per_bar_changed_;
  std::function<void()> on_note_per_beat_changed_;
  std::function<void()> on_closed_;

  std::shared_ptr<perception::ui::Node> window_node_;
  std::shared_ptr<perception::ui::Node> beats_per_bar_input_node_;
  std::shared_ptr<perception::ui::Node> note_per_beat_label_node_;
  std::shared_ptr<perception::ui::Node> note_per_beat_slider_node_;
};

}  // namespace windows

