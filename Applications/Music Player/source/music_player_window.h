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

#include <memory>
#include <string>
#include <string_view>

#include "perception/audio.h"
#include "perception/shared_memory.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/slider.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/node.h"
#include "wav_loader.h"

class MusicPlayerWindow {
 public:
  explicit MusicPlayerWindow(std::string_view initial_file_path = "");
  ~MusicPlayerWindow();

  void LoadFile(std::string_view path);

 private:
  void BuildUI();
  void OpenFileDialog();
  void Play();
  void Pause();
  void TogglePlayPause();
  void SeekTo(double position_seconds);

  void OnTimerTick();
  void DrawWaveform(const perception::ui::DrawContext& context);

  std::string GetFormattedTimeLabel() const;

  std::string file_path_;
  std::string file_name_ = "No file loaded";
  WavData wav_data_;
  bool is_loaded_ = false;

  bool is_playing_ = false;
  double current_time_seconds_ = 0.0;
  double last_playback_start_system_time_ = 0.0;
  double playback_start_offset_ = 0.0;
  perception::AudioStreamID current_stream_id_ = 0;
  std::shared_ptr<perception::SharedMemory> audio_shared_mem_;

  bool window_open_ = true;

  std::shared_ptr<perception::ui::Node> window_node_;
  std::shared_ptr<perception::ui::components::Label> file_name_label_node_;
  std::shared_ptr<perception::ui::components::Label> time_label_node_;
  std::shared_ptr<perception::ui::components::Label> play_label_node_;
  std::shared_ptr<perception::ui::components::Slider> slider_node_;
  std::shared_ptr<perception::ui::Node> waveform_node_;
};
