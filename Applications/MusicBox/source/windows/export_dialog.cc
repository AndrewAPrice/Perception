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
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "perception/scheduler.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/combo_box.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/file_dialog.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/message_dialog.h"
#include "perception/ui/components/segmented_bar.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"
#include "perception/ui/theme.h"
#include "wav_exporter.h"

using ::perception::Defer;
using ::perception::ui::kProgressBarColor;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Button;
using ::perception::ui::components::ComboBox;
using ::perception::ui::components::Container;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::SegmentedBar;
using ::perception::ui::components::ShowMessageDialog;
using ::perception::ui::components::ShowSaveFileDialog;
using ::perception::ui::components::UiWindow;

namespace windows {
namespace {

// Default export WAV file path when no song file path is provided.
constexpr char kDefaultExportPath[] =
    "/Applications/MusicBox/songs/Untitled Song.wav";

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

// Width of the export progress dialog in pixels.
constexpr float kProgressDialogWidth = 360.0f;

struct ExportDialogState {
  std::string export_path;
  int sample_rate_index =
      1;  // 0: 22.05 kHz, 1: 44.1 kHz, 2: 48.0 kHz, 3: 96.0 kHz
  int bit_depth_index =
      1;  // 0: 8-bit PCM, 1: 16-bit PCM, 2: 24-bit PCM, 3: 32-bit Float
  std::shared_ptr<Node> path_input_node;
  std::shared_ptr<Node> info_label_node;
  std::shared_ptr<Node> status_label_node;
  std::weak_ptr<Node> window_node;
};

// State shared between the export progress dialog and the export thread.
struct ExportProgressState {
  std::shared_ptr<Node> window_node;
  std::shared_ptr<SegmentedBar> progress_bar;
  std::shared_ptr<Node> percent_label_node;
  // Set when the progress dialog is closed, telling the export thread to stop.
  std::atomic<bool> cancelled = false;
  // Last whole percentage sent to the UI. Only touched by the export thread.
  int last_reported_percent = -1;
};

// Strong reference to the active export dialog window node.
std::shared_ptr<Node> g_export_dialog_window;

// The export currently running, if any.
std::shared_ptr<ExportProgressState> g_export_progress;

// Updates the progress dialog to show the given percentage.
void SetExportProgress(ExportProgressState& progress, int percent) {
  if (progress.progress_bar)
    progress.progress_bar->SetSegments({{.label = "Exported",
                                         .ratio = percent / 100.0f,
                                         .color = kProgressBarColor}});
  if (progress.percent_label_node)
    if (auto label = progress.percent_label_node->Get<Label>())
      label->SetText(std::to_string(percent) + "%");
}

// Shows a modal progress dialog and exports the song on its own thread, so the
// rest of the app waits for the export without blocking the UI thread.
// Closing the progress dialog cancels the export.
void StartExport(std::shared_ptr<TrackManager> track_snapshot,
                 const std::string& path, const WavExportOptions& options,
                 std::shared_ptr<Node> parent_window) {
  auto progress = std::make_shared<ExportProgressState>();
  std::weak_ptr<ExportProgressState> weak_progress = progress;
  std::string file_name = path.substr(path.rfind('/') + 1);

  progress->window_node = UiWindow::DialogWithTitleBar(
      "Exporting WAV", UiWindow::Parent(parent_window),
      [weak_progress](UiWindow& window) {
        window.OnClose([weak_progress]() {
          if (auto progress = weak_progress.lock()) progress->cancelled = true;
          Defer([]() { g_export_progress.reset(); });
        });
      },
      [](Layout& layout) { layout.SetWidth(kProgressDialogWidth); },
      Container::VerticalContainer(
          Label::BasicLabel("Exporting " + file_name + "..."),
          SegmentedBar::BasicSegmentedBar(progress->progress_bar),
          Label::BasicLabel("0%", &progress->percent_label_node),
          Button::TextButton(
              "Cancel",
              [weak_progress]() {
                if (auto progress = weak_progress.lock())
                  if (auto ui_window = progress->window_node->Get<UiWindow>())
                    ui_window->Close();
              },
              [](Layout& layout) {
                layout.SetWidth(kActionButtonWidth);
                layout.SetAlignSelf(YGAlignFlexEnd);
              })));
  SetExportProgress(*progress, 0);
  g_export_progress = progress;

  std::thread([progress, track_snapshot, path, options, parent_window]() {
    bool succeeded = ExportSongToWav(
        path, *track_snapshot, options, [progress](float fraction) {
          if (progress->cancelled) return false;
          int percent = static_cast<int>(fraction * 100.0f);
          if (percent != progress->last_reported_percent) {
            progress->last_reported_percent = percent;
            Defer([progress, percent]() {
              SetExportProgress(*progress, percent);
            });
          }
          return true;
        });

    Defer([progress, succeeded, path, parent_window]() {
      // The dialog was closed by the user, which already cancelled the export.
      if (progress->cancelled) return;

      if (auto ui_window = progress->window_node->Get<UiWindow>())
        ui_window->Close();
      if (!succeeded)
        ShowMessageDialog("Export Failed",
                          "Unable to export the song to " + path + ".",
                          parent_window);
    });
  }).detach();
}

}  // namespace

void ShowExportWavDialog(
    const TrackManager& track_manager, std::string_view current_song_file_path,
    std::shared_ptr<::perception::ui::Node> parent_window) {
  if (g_export_progress) {
    if (auto ui_win = g_export_progress->window_node->Get<UiWindow>())
      ui_win->Focus();
    return;
  }
  if (g_export_dialog_window) {
    if (auto ui_win = g_export_dialog_window->Get<UiWindow>()) ui_win->Focus();
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
    ::perception::Defer([]() { g_export_dialog_window.reset(); });
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

    double total_bytes =
        kWavHeaderSizeBytes + (total_dur_sec * sr * (bits / 8.0));

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
      [](Layout& layout) { layout.SetWidthPercent(100.0f); },

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
                std::string default_filename = "Untitled Song.wav";
                if (!state->export_path.empty()) {
                  size_t slash = state->export_path.rfind('/');
                  if (slash != std::string::npos) {
                    start_dir = state->export_path.substr(0, slash);
                    default_filename = state->export_path.substr(slash + 1);
                  } else {
                    default_filename = state->export_path;
                  }
                }
                ShowSaveFileDialog(
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
                    {"wav"}, default_filename, start_dir,
                    "Select Export Location", g_export_dialog_window);
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
                  [](Layout& layout) {
                    layout.SetWidth(kSampleRateComboWidth);
                  })),
          Container::HorizontalContainer(
              [](Layout& layout) { layout.SetAlignItems(YGAlignCenter); },
              Label::BasicLabel("Bits per Sample:"),
              ComboBox::BasicComboBox(
                  bit_depth_options, 1,
                  [state, update_info_label](int selected) {
                    state->bit_depth_index = selected;
                    update_info_label();
                  },
                  [](Layout& layout) {
                    layout.SetWidth(kBitDepthComboWidth);
                  }))),

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
              [&track_manager, state, close_dialog, parent_window]() {
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
                int sr_idx =
                    std::clamp(state->sample_rate_index, 0, kNumOptions - 1);
                int bit_idx =
                    std::clamp(state->bit_depth_index, 0, kNumOptions - 1);

                options.sample_rate = kSampleRates[sr_idx];
                options.bits_per_sample = kBitDepths[bit_idx];
                options.is_float = (bit_idx == 3);

                // The export thread works on a copy so the song can't change
                // underneath it.
                auto track_snapshot =
                    std::make_shared<TrackManager>(track_manager);
                close_dialog();
                StartExport(track_snapshot, state->export_path, options,
                            parent_window);
              },
              [](Layout& layout) { layout.SetWidth(kActionButtonWidth); })));

  update_info_label();

  auto dialog_window = UiWindow::DialogWithTitleBar(
      "Export as WAV", UiWindow::Parent(parent_window),
      [close_dialog](UiWindow& window) {
        window.OnClose([close_dialog]() { close_dialog(); });
      },
      [](Layout& layout) { layout.SetWidth(kDialogWidth); }, main_container,
      &state->window_node);

  g_export_dialog_window = dialog_window;
}

}  // namespace windows
