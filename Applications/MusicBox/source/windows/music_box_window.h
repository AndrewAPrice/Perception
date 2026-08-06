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

#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>

#include "metronome.h"
#include "notation/notation_view.h"
#include "panels/keyboard.h"
#include "panels/timeline_ruler.h"
#include "panels/tracks_panel.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/keyboard.h"
#include "perception/ui/node.h"
#include "song_serializer.h"
#include "synth_engine.h"
#include "track_manager.h"
#include "windows/environment_window.h"
#include "windows/export_dialog.h"
#include "windows/help_window.h"
#include "windows/notation_settings_window.h"

#include "undo_manager.h"

namespace windows {

class MusicBoxWindow {
 public:
  explicit MusicBoxWindow(std::string_view initial_song_path = "");
  ~MusicBoxWindow();

 private:
  void BuildUI();
  void UpdateActiveTrackQuickBarUI();
  void ToggleTracksPanelVisibility();
  void ToggleEnvironmentWindow();
  void ToggleMetronome();
  void ToggleNotationSettingsWindow();
  void ToggleKeyboardVisibility();
  void ShowHelpWindow();
  void UpdateTransportButtonsUI();
  void UpdateUndoButtonUI();
  void PerformUndo();
  void SetNotationMode(notation::NotationType type);
  void OnTimerTick();

  void SaveCurrentSong();
  void LoadSongFromPath(std::string_view path);
  void NewSong();
  void ShowExportWavDialog();

  void ShiftTopOctave(int delta);
  void ShiftBottomOctave(int delta);

  void HandleKeyDown(const perception::window::KeyboardKeyEvent& event);
  void HandleKeyUp(const perception::window::KeyboardKeyEvent& event);

  void TriggerNoteOn(int key_index);
  void TriggerNoteOff(int key_index);

  UndoManager undo_manager_;
  TrackManager track_manager_;
  Metronome metronome_;
  SongMetadata current_song_metadata_;
  std::string current_song_file_path_;

  int bottom_octave_ = 3;  // Octave 3 (C3)
  int top_octave_ = 4;     // Octave 4 (C4)

  notation::NotationType current_notation_type_ =
      notation::NotationType::FallingNotes;
  std::unique_ptr<notation::NotationView> notation_view_;

  std::shared_ptr<perception::ui::Node> window_;
  std::shared_ptr<perception::ui::Node> main_focusable_;
  std::shared_ptr<perception::ui::Node> workspace_container_node_;
  std::shared_ptr<perception::ui::Node> resizable_container_node_;
  std::shared_ptr<perception::ui::Node> left_sidebar_node_;
  std::shared_ptr<perception::ui::Node> right_content_node_;
  std::shared_ptr<perception::ui::Node> notation_container_node_;
  std::unique_ptr<panels::Keyboard> keyboard_;
  std::unique_ptr<panels::TimelineRuler> timeline_ruler_;
  std::shared_ptr<perception::ui::Node> active_track_label_node_;
  std::shared_ptr<perception::ui::Node> active_track_instrument_combo_node_;
  std::unique_ptr<panels::TracksPanel> tracks_panel_;
  std::unique_ptr<EnvironmentWindow> environment_window_;
  std::unique_ptr<NotationSettingsWindow> notation_settings_window_;
  std::unique_ptr<HelpWindow> help_window_;
  std::shared_ptr<perception::ui::Node> undo_button_node_;
  std::shared_ptr<perception::ui::Node> tracks_toggle_button_node_;
  std::shared_ptr<perception::ui::Node> stage_button_node_;
  std::shared_ptr<perception::ui::Node> metronome_button_node_;
  std::shared_ptr<perception::ui::Node> settings_button_node_;
  std::shared_ptr<perception::ui::Node> keyboard_button_node_;
  std::shared_ptr<perception::ui::Node> snap_label_node_;
  std::shared_ptr<perception::ui::Node> bpm_input_node_;
  std::shared_ptr<perception::ui::Node> file_button_node_;
  std::shared_ptr<perception::ui::Node> play_button_node_;
  std::shared_ptr<perception::ui::Node> record_button_node_;
  std::shared_ptr<perception::ui::Node> solo_button_node_;
  std::shared_ptr<perception::ui::Node> rewind_button_node_;
  std::shared_ptr<perception::ui::Node> fast_forward_button_node_;

  bool is_tracks_panel_visible_ = true;
  bool is_keyboard_visible_ = true;
  bool is_undoing_ = false;
  bool window_open_ = true;
  std::chrono::time_point<std::chrono::steady_clock> last_tick_time_;
  bool first_tick_ = true;
};

}  // namespace windows
