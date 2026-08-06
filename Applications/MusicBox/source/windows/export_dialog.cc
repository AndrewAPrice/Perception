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

#include "windows/export_dialog.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "perception/ui/components/button.h"
#include "perception/ui/components/combo_box.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/open_file_dialog.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"
#include "wav_exporter.h"

using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Button;
using ::perception::ui::components::ComboBox;
using ::perception::ui::components::Container;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::ShowOpenFileDialog;
using ::perception::ui::components::UiWindow;

namespace windows {
namespace {

// Default export WAV file path when no song file path is provided.
constexpr char kDefaultExportPath[] = "/Applications/MusicBox/songs/Untitled Song.wav";

// Default directory to start browsing for export paths.
constexpr char kDefaultExportDirectory[] = "/Applications/MusicBox/songs";

// Available sample rate options in Hz.
constexpr int kSampleRates[] = {22050, 44100, 48000, 96000};

// Available bit depth options in bits per sample.
constexpr int kBitDepths[] = {8, 16, 24, 32};

// Number of sample rate and bit depth options.
constexpr int kNumOptions = 4;

// Default dialog window width in pixels.
constexpr float kDialogWidth = 460.0f;

// Default dialog window height in pixels.
constexpr float kDialogHeight = 230.0f;

// Width of the Browse button in pixels.
constexpr float kBrowseButtonWidth = 80.0f;

// Width of the Sample Rate combo box in pixels.
constexpr float kSampleRateComboWidth = 110.0f;

// Width of the Bit Depth combo box in pixels.
constexpr float kBitDepthComboWidth = 120.0f;

// Width of action buttons (Cancel, Export) in pixels.
constexpr float kActionButtonWidth = 75.0f;

// Extra tail audio duration buffer in seconds for WAV estimate.
constexpr double kTailDurationBufferSec = 1.5;

// Standard WAV header size in bytes.
constexpr double kWavHeaderSizeBytes = 44.0;

// Default fallback song duration in milliseconds if non-positive.
constexpr int kFallbackSongDurationMs = 2000;

struct ExportDialogState {
  std::string export_path;
  int sample_rate_index = 1;  // 0: 22.05 kHz, 1: 44.1 kHz, 2: 48.0 kHz, 3: 96.0 kHz
  int bit_depth_index = 1;    // 0: 8-bit PCM, 1: 16-bit PCM, 2: 24-bit PCM, 3: 32-bit Float
  std::shared_ptr<Node> path_input_node;
  std::shared_ptr<Node> info_label_node;
  std::shared_ptr<Node> status_label_node;
  std::weak_ptr<Node> window_node;
};

// Weak reference to the active export dialog window node.
std::weak_ptr<Node> g_export_dialog_window;

}  // namespace

void ShowExportWavDialog(const TrackManager& track_manager,
                         std::string_view current_song_file_path) {
  if (auto existing_window = g_export_dialog_window.lock()) {
    if (auto ui_win = existing_window->Get<UiWindow>())
      ui_win->Focus();
    return;
  }

  std::string default_path = kDefaultExportPath;
  if (!current_song_file_path.empty()) {
    std::string base = std::string(current_song_file_path);
    if (base.size() >= 5 && base.compare(base.size() - 5, 5, ".song") == 0)
      base = base.substr(0, base.size() - 5);
    default_path = base + ".wav";
  }

  auto state = std::make_shared<ExportDialogState>();
  state->export_path = default_path;

  auto close_dialog = [state]() {
    if (auto win = state->window_node.lock())
      if (auto ui_win = win->Get<UiWindow>()) ui_win->Close();
  };

  auto update_info_label = [&track_manager, state]() {
    int sr_idx = std::clamp(state->sample_rate_index, 0, kNumOptions - 1);
    int bit_idx = std::clamp(state->bit_depth_index, 0, kNumOptions - 1);

    int sr = kSampleRates[sr_idx];
    int bits = kBitDepths[bit_idx];

    int bitrate_kbps = (sr * bits) / 1000;

    int song_dur_ms = track_manager.GetSongDurationMs();
    if (song_dur_ms <= 0) song_dur_ms = kFallbackSongDurationMs;
    double total_dur_sec = (song_dur_ms / 1000.0) + kTailDurationBufferSec;

    double total_bytes = kWavHeaderSizeBytes + (total_dur_sec * sr * (bits / 8.0));

    char buf[128];
    if (total_bytes >= 1024.0 * 1024.0) {
      std::snprintf(buf, sizeof(buf), "Bitrate: %d kbps  |  Est. Size: %.2f MB",
                    bitrate_kbps, total_bytes / (1024.0 * 1024.0));
    } else {
      std::snprintf(buf, sizeof(buf), "Bitrate: %d kbps  |  Est. Size: %.1f KB",
                    bitrate_kbps, total_bytes / 1024.0);
    }

    if (state->info_label_node) {
      if (auto lbl = state->info_label_node->Get<Label>()) lbl->SetText(buf);
    }
  };

  std::vector<std::string> sample_rate_options = {"22.05 kHz", "44.1 kHz",
                                                  "48.0 kHz", "96.0 kHz"};
  std::vector<std::string> bit_depth_options = {"8-bit PCM", "16-bit PCM",
                                                "24-bit PCM", "32-bit Float"};

  auto main_container = Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetHeightPercent(100.0f);
      },

      // Export Path Row
      Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetAlignItems(YGAlignCenter);
            layout.SetWidthPercent(100.0f);
          },
          Label::BasicLabel("Export Path:"),
          InputBox::BasicInputBox(
              default_path,
              [state](InputBox& input_box) {
                input_box.OnTextChanged([state](std::string_view text) {
                  state->export_path = std::string(text);
                });
              },
              [](Layout& layout) { layout.SetFlexGrow(1.0f); },
              &state->path_input_node),
          Button::TextButton(
              "Browse...",
              [state]() {
                std::string start_dir = kDefaultExportDirectory;
                if (!state->export_path.empty()) {
                  size_t slash = state->export_path.rfind('/');
                  if (slash != std::string::npos)
                    start_dir = state->export_path.substr(0, slash);
                }
                ShowOpenFileDialog(
                    [state](bool succeeded, std::string_view path) {
                      if (succeeded && !path.empty()) {
                        state->export_path = std::string(path);
                        if (state->path_input_node) {
                          if (auto input =
                                  state->path_input_node->Get<InputBox>())
                            input->SetText(state->export_path);
                        }
                      }
                    },
                    {"wav"}, start_dir, "Select Export Location");
              },
              [](Layout& layout) { layout.SetWidth(kBrowseButtonWidth); })),

      // Format Settings Row: Sample Rate & Bits Per Sample
      Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetAlignItems(YGAlignCenter);
            layout.SetWidthPercent(100.0f);
          },
          Container::HorizontalContainer(
              [](Layout& layout) { layout.SetAlignItems(YGAlignCenter); },
              Label::BasicLabel("Sample Rate:"),
              ComboBox::BasicComboBox(
                  sample_rate_options, 1,
                  [state, update_info_label](int selected) {
                    state->sample_rate_index = selected;
                    update_info_label();
                  },
                  [](Layout& layout) { layout.SetWidth(kSampleRateComboWidth); })),
          Container::HorizontalContainer(
              [](Layout& layout) { layout.SetAlignItems(YGAlignCenter); },
              Label::BasicLabel("Bits per Sample:"),
              ComboBox::BasicComboBox(
                  bit_depth_options, 1,
                  [state, update_info_label](int selected) {
                    state->bit_depth_index = selected;
                    update_info_label();
                  },
                  [](Layout& layout) { layout.SetWidth(kBitDepthComboWidth); }))),

      // Bitrate & File Size Info Label
      Label::BasicLabel(
          "Bitrate: 1411 kbps  |  Est. Size: 0.0 MB",
          [](Layout& layout) { layout.SetMargin(YGEdgeTop, 2.0f); },
          [](Label& label) {}, &state->info_label_node),

      // Status Label
      Label::BasicLabel(
          "", [](Layout& layout) { layout.SetMargin(YGEdgeTop, 2.0f); },
          [](Label& label) {}, &state->status_label_node),

      // Bottom Control Buttons Row
      Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetJustifyContent(YGJustifyFlexEnd);
            layout.SetWidthPercent(100.0f);
          },
          Button::TextButton(
              "Cancel", [close_dialog]() { close_dialog(); },
              [](Layout& layout) { layout.SetWidth(kActionButtonWidth); }),
          Button::TextButton(
              "Export",
              [&track_manager, state, close_dialog]() {
                if (state->path_input_node) {
                  if (auto input = state->path_input_node->Get<InputBox>())
                    state->export_path = input->GetText();
                }

                if (state->export_path.empty()) {
                  if (state->status_label_node) {
                    if (auto lbl = state->status_label_node->Get<Label>())
                      lbl->SetText("Error: Invalid destination path.");
                  }
                  return;
                }

                WavExportOptions options;
                int sr_idx = std::clamp(state->sample_rate_index, 0, kNumOptions - 1);
                int bit_idx = std::clamp(state->bit_depth_index, 0, kNumOptions - 1);

                options.sample_rate = kSampleRates[sr_idx];
                options.bits_per_sample = kBitDepths[bit_idx];
                options.is_float = (bit_idx == 3);

                bool success = ExportSongToWav(state->export_path,
                                               track_manager, options);
                if (success) {
                  close_dialog();
                } else {
                  if (state->status_label_node) {
                    if (auto lbl = state->status_label_node->Get<Label>())
                      lbl->SetText("Error: Failed to export WAV file.");
                  }
                }
              },
              [](Layout& layout) { layout.SetWidth(kActionButtonWidth); })));

  update_info_label();

  auto dialog_window = UiWindow::DialogWithTitleBar(
      "Export as WAV",
      [close_dialog](UiWindow& window) {
        window.OnClose([close_dialog]() { close_dialog(); });
      },
      [](Layout& layout) {
        layout.SetWidth(kDialogWidth);
        layout.SetHeight(kDialogHeight);
      },
      main_container, &state->window_node);

  g_export_dialog_window = dialog_window;
}

}  // namespace windows
