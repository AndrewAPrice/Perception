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

#include "music_player_window.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "perception/fibers.h"
#include "perception/processes.h"
#include "perception/time.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/open_file_dialog.h"
#include "perception/ui/components/slider.h"
#include "perception/ui/components/ui_window.h"

using ::perception::SleepForDuration;
using ::perception::TerminateProcess;
using ::perception::ui::DrawContext;
using ::perception::ui::Layout;
using ::perception::ui::Point;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::Label;
using ::perception::ui::components::ShowOpenFileDialog;
using ::perception::ui::components::Slider;
using ::perception::ui::components::UiWindow;

namespace {

constexpr float kWindowWidth = 400.0f;

constexpr float kWaveformMinHeight = 180.0f;
constexpr float kWaveformBorderRadius = 8.0f;
constexpr float kWaveformHeightScale = 0.85f;
constexpr float kWaveformStrokeWidth = 1.5f;
constexpr float kMinBinHeight = 1.0f;
constexpr float kMinBinHalfHeight = 0.5f;

constexpr float kPlayheadStrokeWidth = 2.0f;

constexpr uint32_t kBackgroundColor = 0xFF1E293B;  // Dark slate background
constexpr uint32_t kPlayedColor = 0xFF06B6D4;      // Vibrant Cyan
constexpr uint32_t kUnplayedColor = 0xFF64748B;    // Muted Slate Gray
constexpr uint32_t kPlayheadColor = 0xFFF59E0B;    // Amber Yellow playhead

constexpr double kEndThresholdSeconds = 0.05;
constexpr auto kTimerTickInterval = std::chrono::milliseconds(50);

}  // namespace

MusicPlayerWindow::MusicPlayerWindow(std::string_view initial_file_path) {
  BuildUI();
  if (!initial_file_path.empty()) {
    LoadFile(initial_file_path);
    Play();
  } else {
    OpenFileDialog();
  }
  perception::Fiber::Create([this]() { OnTimerTick(); })->WakeUp();
}

MusicPlayerWindow::~MusicPlayerWindow() {
  window_open_ = false;
  if (is_playing_) Pause();
}

std::string MusicPlayerWindow::GetFormattedTimeLabel() const {
  if (!is_loaded_) return "0s / 0s";
  return perception::FormatTime(current_time_seconds_) + " / " +
         perception::FormatTime(wav_data_.duration_seconds);
}

void MusicPlayerWindow::BuildUI() {
  waveform_node_ = perception::ui::Node::Empty(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetMinHeight(kWaveformMinHeight);
        layout.SetWidthPercent(100.0f);
      },
      [](Block& block) {
        block.SetBorderRadius(kWaveformBorderRadius);
        block.SetFillColor(kBackgroundColor);
      },
      [this](perception::ui::Node& node) {
        node.OnDraw(
            [this](const DrawContext& context) { DrawWaveform(context); });
        node.OnMouseButtonDown(
            [this](const Point& point, perception::window::MouseButton button) {
              if (!is_loaded_ || wav_data_.duration_seconds <= 0.0) return;
              float width = waveform_node_->GetSize().width;
              if (width > 0.0f) {
                double ratio =
                    std::clamp(static_cast<double>(point.x) / width, 0.0, 1.0);
                SeekTo(ratio * wav_data_.duration_seconds);
              }
            });
      });

  window_node_ = UiWindow::ResizableWindowWithTitleBar(
      "Music Player", [](Layout& layout) { layout.SetWidth(kWindowWidth); },
      [this](UiWindow& window) {
        window.OnClose([]() { TerminateProcess(); });
      },
      // Header: File title and Open button
      Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetAlignItems(YGAlignCenter);
            layout.SetJustifyContent(YGJustifySpaceBetween);
            layout.SetWidthPercent(100.0f);
          },
          Label::BasicLabel(
              "No file loaded",
              [](Layout& layout) { layout.SetFlexGrow(1.0f); },
              &file_name_label_node_),
          Button::TextButton("Open WAV", [this]() { OpenFileDialog(); })),
      // Waveform node in the middle
      waveform_node_,
      // Timeline slider
      Slider::BasicSlider(
          0.0f, 100.0f, 0.0f,
          [this](float new_pos) { SeekTo(static_cast<double>(new_pos)); },
          [](Layout& layout) { layout.SetWidthPercent(100.0f); },
          &slider_node_),
      // Bottom Controls Bar: Play/Pause button and position label
      Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetAlignItems(YGAlignCenter);
            layout.SetJustifyContent(YGJustifySpaceBetween);
            layout.SetWidthPercent(100.0f);
          },
          Button::BasicButton(
              [this]() { TogglePlayPause(); },
              Label::SingleLineTruncated("Play", &play_label_node_)),
          Label::BasicLabel("0s / 0s", &time_label_node_)));
}

void MusicPlayerWindow::OpenFileDialog() {
  ShowOpenFileDialog(
      [this](bool succeeded, std::string_view path) {
        if (succeeded && !path.empty()) {
          LoadFile(path);
        }
      },
      {".wav"}, "", "Open WAV Audio File");
}

void MusicPlayerWindow::LoadFile(std::string_view path) {
  if (is_playing_) {
    Pause();
  }

  file_path_ = std::string(path);
  file_name_ = std::filesystem::path(file_path_).filename().string();

  WavData new_wav;
  if (!LoadWavFile(file_path_, new_wav)) {
    is_loaded_ = false;
    file_name_label_node_->SetText("Error loading: " + file_name_);
    return;
  }

  wav_data_ = std::move(new_wav);
  is_loaded_ = true;
  current_time_seconds_ = 0.0;
  playback_start_offset_ = 0.0;

  file_name_label_node_->SetText(file_name_);
  time_label_node_->SetText(GetFormattedTimeLabel());
  slider_node_->SetRange(0.0f, static_cast<float>(wav_data_.duration_seconds));
  slider_node_->SetValue(0.0f);
  waveform_node_->Invalidate();
}

void MusicPlayerWindow::Play() {
  if (!is_loaded_ || wav_data_.duration_seconds <= 0.0) return;
  if (is_playing_) return;

  if (current_time_seconds_ >=
      wav_data_.duration_seconds - kEndThresholdSeconds) {
    current_time_seconds_ = 0.0;
  }

  size_t frame_offset =
      static_cast<size_t>(current_time_seconds_ * wav_data_.sample_rate);
  size_t byte_offset = frame_offset * wav_data_.channels * sizeof(int16_t);

  if (byte_offset >= wav_data_.pcm_bytes.size()) return;

  size_t remaining_bytes = wav_data_.pcm_bytes.size() - byte_offset;
  audio_shared_mem_ = perception::SharedMemory::FromSize(remaining_bytes, 0);

  if (!audio_shared_mem_ || **audio_shared_mem_ == nullptr) return;

  std::memcpy(**audio_shared_mem_, wav_data_.pcm_bytes.data() + byte_offset,
              remaining_bytes);

  current_stream_id_ =
      perception::PlayAudio(audio_shared_mem_, 1.0f, false,
                            wav_data_.sample_rate, wav_data_.channels, 16);

  last_playback_start_system_time_ = getTime();
  playback_start_offset_ = current_time_seconds_;
  is_playing_ = true;

  play_label_node_->SetText("Pause");
}

void MusicPlayerWindow::Pause() {
  if (!is_playing_) return;

  if (current_stream_id_ != 0) {
    perception::StopAudio(current_stream_id_);
    current_stream_id_ = 0;
  }

  is_playing_ = false;

  play_label_node_->SetText("Play");
}

void MusicPlayerWindow::TogglePlayPause() {
  if (is_playing_) {
    Pause();
  } else {
    Play();
  }
}

void MusicPlayerWindow::SeekTo(double position_seconds) {
  if (!is_loaded_) return;

  position_seconds =
      std::clamp(position_seconds, 0.0, wav_data_.duration_seconds);
  bool was_playing = is_playing_;

  if (was_playing) {
    Pause();
  }

  current_time_seconds_ = position_seconds;
  playback_start_offset_ = position_seconds;

  slider_node_->SetValue(static_cast<float>(current_time_seconds_));
  time_label_node_->SetText(GetFormattedTimeLabel());
  waveform_node_->Invalidate();

  if (was_playing) Play();
}

void MusicPlayerWindow::OnTimerTick() {
  while (window_open_) {
    if (is_playing_) {
      double elapsed = getTime() - last_playback_start_system_time_;
      current_time_seconds_ = playback_start_offset_ + elapsed;

      if (current_time_seconds_ >= wav_data_.duration_seconds) {
        current_time_seconds_ = wav_data_.duration_seconds;
        Pause();
        current_time_seconds_ = 0.0;
        playback_start_offset_ = 0.0;
      }

      slider_node_->SetValue(static_cast<float>(current_time_seconds_));
      time_label_node_->SetText(GetFormattedTimeLabel());
      waveform_node_->Invalidate();
    }
    SleepForDuration(kTimerTickInterval);
  }
}

void MusicPlayerWindow::DrawWaveform(const DrawContext& context) {
  SkCanvas* canvas = context.skia_canvas;
  if (!canvas) return;

  float width = context.area.size.width;
  float height = context.area.size.height;
  float x_offset = context.area.origin.x;
  float y_offset = context.area.origin.y;

  // Background
  SkPaint bg_paint;
  bg_paint.setColor(kBackgroundColor);
  bg_paint.setAntiAlias(true);
  SkRect bg_rect = SkRect::MakeXYWH(x_offset, y_offset, width, height);
  canvas->drawRRect(SkRRect::MakeRectXY(bg_rect, kWaveformBorderRadius,
                                        kWaveformBorderRadius),
                    bg_paint);

  if (!is_loaded_ || wav_data_.samples_mono.empty() || width <= 0.0f ||
      height <= 0.0f) {
    return;
  }

  size_t total_samples = wav_data_.samples_mono.size();
  int num_bins = static_cast<int>(width);
  float center_y = y_offset + height / 2.0f;
  float max_half_h = (height / 2.0f) * kWaveformHeightScale;

  float playhead_ratio = (wav_data_.duration_seconds > 0.0)
                             ? static_cast<float>(current_time_seconds_ /
                                                  wav_data_.duration_seconds)
                             : 0.0f;
  float playhead_x = x_offset + playhead_ratio * width;

  SkPaint played_paint;
  played_paint.setColor(kPlayedColor);
  played_paint.setStrokeWidth(kWaveformStrokeWidth);
  played_paint.setAntiAlias(true);

  SkPaint unplayed_paint;
  unplayed_paint.setColor(kUnplayedColor);
  unplayed_paint.setStrokeWidth(kWaveformStrokeWidth);
  unplayed_paint.setAntiAlias(true);

  double samples_per_bin = static_cast<double>(total_samples) / num_bins;

  for (int bin = 0; bin < num_bins; ++bin) {
    size_t start_idx = static_cast<size_t>(bin * samples_per_bin);
    size_t end_idx = static_cast<size_t>((bin + 1) * samples_per_bin);
    end_idx = std::min(end_idx, total_samples);

    if (start_idx >= total_samples) break;

    float min_s = 0.0f;
    float max_s = 0.0f;

    for (size_t i = start_idx; i < end_idx; ++i) {
      float s = wav_data_.samples_mono[i];
      if (s < min_s) min_s = s;
      if (s > max_s) max_s = s;
    }

    float bin_x = x_offset + bin;
    float y1 = center_y - (max_s * max_half_h);
    float y2 = center_y - (min_s * max_half_h);

    if (std::abs(y2 - y1) < kMinBinHeight) {
      y1 = center_y - kMinBinHalfHeight;
      y2 = center_y + kMinBinHalfHeight;
    }

    canvas->drawLine(bin_x, y1, bin_x, y2,
                     bin_x <= playhead_x ? played_paint : unplayed_paint);
  }

  // Draw playhead cursor line
  SkPaint playhead_paint;
  playhead_paint.setColor(kPlayheadColor);
  playhead_paint.setStrokeWidth(kPlayheadStrokeWidth);
  playhead_paint.setAntiAlias(true);
  canvas->drawLine(playhead_x, y_offset, playhead_x, y_offset + height,
                   playhead_paint);
}
