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

#include "windows/music_box_window.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "notation/falling_notes_view.h"
#include "notation/notation_view.h"
#include "notation/sheet_view.h"
#include "perception/fibers.h"
#include "perception/processes.h"
#include "perception/registry.h"
#include "perception/scheduler.h"
#include "perception/time.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/combo_box.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/focusable.h"
#include "perception/ui/components/image_button.h"
#include "perception/ui/components/image_view.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/open_file_dialog.h"
#include "perception/ui/components/pop_up.h"
#include "perception/ui/components/resizable_container.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/slider.h"
#include "perception/ui/components/tooltip.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/image.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "song_serializer.h"
#include "wav_exporter.h"
#include "windows/environment_window.h"

using ::perception::HandOverControl;
using ::perception::TerminateProcess;
using ::perception::ui::DrawContext;
using ::perception::ui::Image;
using ::perception::ui::KeyCode;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::Size;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::ComboBox;
using ::perception::ui::components::Container;
using ::perception::ui::components::Focusable;
using ::perception::ui::components::ImageButton;
using ::perception::ui::components::ImageView;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::PopUp;
using ::perception::ui::components::PopUpMenu;
using ::perception::ui::components::ResizableContainer;
using ::perception::ui::components::ResizableContainerItem;
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::ShowOpenFileDialog;
using ::perception::ui::components::Slider;
using ::perception::ui::components::Tooltip;
using ::perception::ui::components::UiWindow;

namespace windows {

using namespace notation;
using namespace panels;

namespace {

// Human-readable labels for note duration snap quantization settings.
constexpr const char* kSnapNames[12] = {"1/256", "1/128", "1/64", "1/32",
                                        "1/16",  "1/8",   "1/4",  "1/2",
                                        "1",     "2",     "4",    "8"};

std::shared_ptr<Image> GetKeyboardImage() {
  static auto keyboard_img =
      Image::LoadImage("/Applications/MusicBox/keyboard.svg");
  return keyboard_img;
}

std::shared_ptr<Image> GetTracksImage() {
  static auto tracks_img =
      Image::LoadImage("/Applications/MusicBox/tracks.svg");
  return tracks_img;
}

std::shared_ptr<Image> GetUndoImage() {
  static auto undo_img = Image::LoadImage("/Applications/MusicBox/undo.svg");
  return undo_img;
}

}  // namespace

MusicBoxWindow::MusicBoxWindow(std::string_view initial_song_path) {
  StartAudioStream();
  metronome_.Initialize();
  undo_manager_.SetOnStackChangedCallback([this]() { UpdateUndoButtonUI(); });
  track_manager_.SetOnRecordFinishedCallback(
      [this](int track_id, const std::vector<NoteEvent>& old_notes) {
        undo_manager_.PushAction(
            std::make_unique<RecordUndoAction>(track_id, old_notes));
      });
  BuildUI();
  if (!initial_song_path.empty()) {
    LoadSongFromPath(initial_song_path);
  } else {
    NewSong();
  }

  auto val = ::perception::GetRegistryValue("welcome_shown");
  if (!val.Ok() || !val->BoolValue().value_or(false)) {
    ::perception::SetRegistryValue("welcome_shown", true);
    ShowHelpWindow();
  }

  perception::Fiber::Create([this]() { OnTimerTick(); })->WakeUp();
}

MusicBoxWindow::~MusicBoxWindow() { StopAudioStream(); }

void MusicBoxWindow::LoadSongFromPath(std::string_view path) {
  track_manager_.Stop();
  track_manager_.Seek(0);
  undo_manager_.Clear();
  current_song_file_path_ = std::string(path);
  bool success = LoadSongFromFile(current_song_file_path_, track_manager_,
                                  &current_song_metadata_);
  if (success) {
    if (bpm_input_node_) {
      if (auto input_box = bpm_input_node_->Get<InputBox>()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f", current_song_metadata_.bpm);
        input_box->SetText(buf);
      }
    }
    if (notation_view_) {
      notation_view_->SetBeatsPerBar(current_song_metadata_.beats_per_bar);
      notation_view_->SetNotePerBeat(current_song_metadata_.note_per_beat);
    }
    if (notation_settings_window_) {
      notation_settings_window_->UpdateValues(current_song_metadata_);
    }
  }
  if (tracks_panel_) tracks_panel_->UpdateTrackListUI();
  UpdateActiveTrackQuickBarUI();
  if (notation_view_)
    notation_view_->OnTrackSelected(track_manager_.GetActiveTrackId(),
                                    /*auto_scroll=*/true);
  UpdateTransportButtonsUI();
  UpdateUndoButtonUI();
}

void MusicBoxWindow::NewSong() {
  track_manager_.Stop();
  track_manager_.Seek(0);
  undo_manager_.Clear();
  current_song_file_path_.clear();
  track_manager_.ClearAllTracks();
  track_manager_.AddTrack("Track 1", GetDefaultInstrument(),
                          GetTrackPresetColor(0));
  current_song_metadata_ = SongMetadata{.title = "Untitled Song",
                                        .bpm = 120.0f,
                                        .beats_per_bar = 4,
                                        .note_per_beat = 4};
  track_manager_.SetBpm(120.0f);
  if (bpm_input_node_) {
    if (auto input_box = bpm_input_node_->Get<InputBox>()) {
      input_box->SetText("120");
    }
  }
  if (notation_view_) {
    notation_view_->SetBeatsPerBar(4);
    notation_view_->SetNotePerBeat(4);
  }
  if (notation_settings_window_) {
    notation_settings_window_->UpdateValues(current_song_metadata_);
  }
  if (tracks_panel_) tracks_panel_->UpdateTrackListUI();
  UpdateActiveTrackQuickBarUI();
  if (notation_view_)
    notation_view_->OnTrackSelected(track_manager_.GetActiveTrackId(),
                                    /*auto_scroll=*/true);
  UpdateTransportButtonsUI();
  UpdateUndoButtonUI();
}

void MusicBoxWindow::SaveCurrentSong() {
  std::string default_dir = "/Applications/MusicBox/songs";
  if (!current_song_file_path_.empty()) {
    size_t slash = current_song_file_path_.rfind('/');
    if (slash != std::string::npos) {
      default_dir = current_song_file_path_.substr(0, slash);
    }
  }

  ShowOpenFileDialog(
      [this](bool succeeded, std::string_view path) {
        if (succeeded && !path.empty()) {
          current_song_file_path_ = std::string(path);
          current_song_metadata_.bpm = track_manager_.GetBpm();
          SaveSongToFile(current_song_file_path_, track_manager_,
                         current_song_metadata_);
        }
      },
      {"song"}, default_dir, "Save Song");
}

void MusicBoxWindow::ShowExportWavDialog() {
  windows::ShowExportWavDialog(track_manager_, current_song_file_path_);
}

void MusicBoxWindow::BuildUI() {
  tracks_panel_ = std::make_unique<TracksPanel>(track_manager_, [this]() {
    UpdateActiveTrackQuickBarUI();
    if (notation_view_)
      notation_view_->OnTrackSelected(track_manager_.GetActiveTrackId(),
                                      /*auto_scroll=*/true);
  });
  tracks_panel_->SetUndoManager(&undo_manager_);

  keyboard_ = std::make_unique<Keyboard>(track_manager_);
  keyboard_->SetOctaves(bottom_octave_, top_octave_);
  keyboard_->OnOctavesChanged([this](int bottom_octave, int top_octave) {
    bottom_octave_ = bottom_octave;
    top_octave_ = top_octave;
  });
  keyboard_->OnKeyDown([this](int key) { TriggerNoteOn(key); });
  keyboard_->OnKeyUp([this](int key) { TriggerNoteOff(key); });

  timeline_ruler_ = std::make_unique<TimelineRuler>(track_manager_, [this]() {
    if (notation_view_) notation_view_->Invalidate();
  });

  if (keyboard_->GetNode()) {
    auto k_focusable = keyboard_->GetNode()->GetOrAdd<Focusable>();
    k_focusable->OnKeyDown(
        [this](const perception::window::KeyboardKeyEvent& event) {
          HandleKeyDown(event);
        });
    k_focusable->OnKeyUp(
        [this](const perception::window::KeyboardKeyEvent& event) {
          HandleKeyUp(event);
        });
  }

  std::shared_ptr<Focusable> main_focusable_comp;

  // Notation options
  std::vector<std::string> notation_options = {"Falling Notes", "Sheet"};

  window_ = UiWindow::ResizableWindowWithTitleBar(
      "MusicBox",
      [](Layout& layout) {
        layout.SetWidth(880.0f);
        layout.SetHeight(520.0f);
      },
      [this](UiWindow& window) {
        window.OnClose([this]() {
          window_open_ = false;
          TerminateProcess();
        });
      },
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetAlignSelf(YGAlignStretch);
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
            layout.SetMinHeight(0.0f);
            layout.SetMinWidth(0.0f);
          },
          [](Focusable& focusable) {}, &main_focusable_comp, &main_focusable_,

          // --- Top Control Bar (Pill Grouped Toolbar) ---
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetAlignSelf(YGAlignStretch);
                layout.SetJustifyContent(YGJustifySpaceBetween);
                layout.SetAlignItems(YGAlignCenter);
                layout.SetMargin(YGEdgeBottom, 4.0f);
              },

              // Group 1: File Dropdown, Undo Button, Tracks Panel Toggle,
              // Keyboard Toggle
              Container::HorizontalContainer(
                  [](Layout& layout) {
                    layout.SetAlignItems(YGAlignCenter);
                    layout.SetGap(4.0f);
                  },
                  Button::TextButton(
                      "File",
                      [this]() {
                        if (!file_button_node_) return;
                        Point pos = file_button_node_->GetAbsolutePosition();
                        float height = file_button_node_->GetSize().height;
                        auto menu = PopUpMenu::Container(
                            PopUpMenu::DropDownItem("New Song",
                                                    [this]() { NewSong(); }),
                            PopUpMenu::DropDownItem(
                                "Load Song",
                                [this]() {
                                  ShowOpenFileDialog(
                                      [this](bool succeeded,
                                             std::string_view path) {
                                        if (succeeded && !path.empty())
                                          LoadSongFromPath(path);
                                      },
                                      {"song"}, "/Applications/MusicBox/songs",
                                      "Load Song");
                                }),
                            PopUpMenu::DropDownItem(
                                "Save Song", [this]() { SaveCurrentSong(); }),
                            PopUpMenu::DropDownItem(
                                "Export as WAV...",
                                [this]() { ShowExportWavDialog(); }),
                            PopUpMenu::DropDownItem(
                                "Help", [this]() { ShowHelpWindow(); }));
                        PopUp::Show(file_button_node_,
                                    Point{pos.x, pos.y + height}, menu);
                      },
                      [](Layout& layout) { layout.SetWidth(50.0f); },
                      &file_button_node_),
                  ImageButton::BasicImageButton(
                      [this]() { PerformUndo(); }, GetUndoImage(),
                      [this](Button& button) {
                        button.SetButtonStyle(
                            undo_manager_.CanUndo()
                                ? Button::ButtonStyle::GHOST
                                : Button::ButtonStyle::DISABLED);
                      },
                      Tooltip::ShowTooltip("Undo"),
                      [](Layout& layout) {
                        layout.SetWidth(28.0f);
                        layout.SetHeight(28.0f);
                      },
                      &undo_button_node_),
                  ImageButton::BasicImageButton(
                      [this]() { ToggleTracksPanelVisibility(); },
                      GetTracksImage(),
                      [this](Button& button) {
                        button.SetToggled(is_tracks_panel_visible_);
                      },
                      Tooltip::ShowTooltip("Toggle Left Tracks Panel"),
                      [](Layout& layout) {
                        layout.SetWidth(28.0f);
                        layout.SetHeight(28.0f);
                      },
                      &tracks_toggle_button_node_),
                  ImageButton::BasicImageButton(
                      [this]() { ToggleKeyboardVisibility(); },
                      GetKeyboardImage(),
                      [this](Button& button) {
                        button.SetToggled(is_keyboard_visible_);
                      },
                      Tooltip::ShowTooltip("Toggle Piano Keyboard"),
                      [](Layout& layout) {
                        layout.SetWidth(28.0f);
                        layout.SetHeight(28.0f);
                      },
                      &keyboard_button_node_)),

              // Group 2: Transport Buttons, Global Solo, Time Counter
              Container::HorizontalContainer(
                  [](Layout& layout) {
                    layout.SetAlignItems(YGAlignCenter);
                    layout.SetGap(4.0f);
                  },
                  Button::TextButton(
                      "|◀",
                      [this]() {
                        if (track_manager_.GetCurrentTimeMs() <= 0) return;
                        track_manager_.Seek(0);
                        metronome_.Reset();
                        UpdateTransportButtonsUI();
                        if (notation_view_) notation_view_->Invalidate();
                        if (keyboard_) keyboard_->Invalidate();
                      },
                      Tooltip::ShowTooltip("Rewind to start"),
                      &rewind_button_node_),
                  Button::TextButton(
                      "▶|",
                      [this]() {
                        if (track_manager_.GetCurrentTimeMs() >=
                            track_manager_.GetSongDurationMs())
                          return;
                        track_manager_.Seek(track_manager_.GetSongDurationMs());
                        metronome_.Reset();
                        UpdateTransportButtonsUI();
                        if (notation_view_) notation_view_->Invalidate();
                        if (keyboard_) keyboard_->Invalidate();
                      },
                      Tooltip::ShowTooltip("Fast forward to end"),
                      &fast_forward_button_node_),
                  Button::TextButton(
                      "▶",
                      [this]() {
                        if (track_manager_.IsRecording()) return;
                        if (track_manager_.IsPlaying()) {
                          track_manager_.Stop();
                          metronome_.Reset();
                        } else {
                          if (track_manager_.GetCurrentTimeMs() >=
                              track_manager_.GetSongDurationMs()) {
                            track_manager_.Seek(0);
                          }
                          first_tick_ = true;
                          metronome_.Reset();
                          track_manager_.StartPlay();
                        }
                        UpdateTransportButtonsUI();
                      },
                      Tooltip::ShowTooltip("Play / Pause"), &play_button_node_),
                  Button::TextButton(
                      "●",
                      [this]() {
                        if (track_manager_.IsPlaying() &&
                            !track_manager_.IsRecording()) {
                          return;
                        }
                        if (track_manager_.IsRecording()) {
                          track_manager_.Stop();
                          metronome_.Reset();
                        } else {
                          first_tick_ = true;
                          metronome_.Reset();
                          track_manager_.StartRecord();
                        }
                        UpdateTransportButtonsUI();
                      },
                      Tooltip::ShowTooltip("Record"), &record_button_node_),
                  Button::TextButton(
                      "S",
                      [this]() {
                        track_manager_.ToggleGlobalSolo();
                        UpdateTransportButtonsUI();
                        if (notation_view_) notation_view_->Invalidate();
                        if (keyboard_) keyboard_->Invalidate();
                      },
                      [this](Button& button) {
                        button.SetToggled(track_manager_.IsGlobalSoloActive());
                      },
                      Tooltip::ShowTooltip("Global Solo Toggle"),
                      [](Layout& layout) { layout.SetWidth(28.0f); },
                      &solo_button_node_)),

              // Group 3: BPM Input & Metronome Toggle
              Container::HorizontalContainer(
                  [](Layout& layout) {
                    layout.SetAlignItems(YGAlignCenter);
                    layout.SetGap(4.0f);
                  },
                  Label::BasicLabel("BPM:"),
                  InputBox::BasicInputBox(
                      "120",
                      [this](InputBox& input_box) {
                        input_box.OnTextChanged([this](std::string_view text) {
                          if (is_undoing_) return;
                          try {
                            float bpm = std::stof(std::string(text));
                            if (std::abs(bpm - track_manager_.GetBpm()) >
                                0.001f) {
                              SongSettingsState old_s{
                                  .bpm = track_manager_.GetBpm(),
                                  .beats_per_bar =
                                      current_song_metadata_.beats_per_bar,
                                  .note_per_beat =
                                      current_song_metadata_.note_per_beat};
                              SongSettingsState new_s{
                                  .bpm = bpm,
                                  .beats_per_bar =
                                      current_song_metadata_.beats_per_bar,
                                  .note_per_beat =
                                      current_song_metadata_.note_per_beat};
                              undo_manager_.PushAction(
                                  std::make_unique<
                                      ChangeSongSettingsUndoAction>(old_s,
                                                                    new_s));
                              track_manager_.SetBpm(bpm);
                              current_song_metadata_.bpm = bpm;
                              UpdateUndoButtonUI();
                            }
                          } catch (...) {
                          }
                        });
                      },
                      [](Layout& layout) { layout.SetWidth(42.0f); },
                      &bpm_input_node_),
                  [&]() {
                    static auto metronome_img = Image::LoadImage(
                        "/Applications/MusicBox/metronome.svg");
                    return ImageButton::BasicImageButton(
                        [this]() { ToggleMetronome(); }, metronome_img,
                        Tooltip::ShowTooltip("Toggle Metronome"),
                        [](Layout& layout) {
                          layout.SetWidth(28.0f);
                          layout.SetHeight(28.0f);
                        },
                        &metronome_button_node_);
                  }()),

              // Group 4: Notation Mode, Snap Grid, Zoom, Environment & Settings
              Container::HorizontalContainer(
                  [](Layout& layout) {
                    layout.SetAlignItems(YGAlignCenter);
                    layout.SetGap(6.0f);
                  },
                  ComboBox::BasicComboBox(
                      notation_options, 0,
                      [this](int selected) {
                        SetNotationMode(selected == 1
                                            ? NotationType::Staff
                                            : NotationType::FallingNotes);
                      },
                      [](Layout& layout) { layout.SetWidth(110.0f); }),
                  Label::BasicLabel("Snap:"),
                  Label::BasicLabel("1/16", &snap_label_node_),
                  Slider::BasicSlider(
                      0.0f, 11.0f, 4.0f,
                      [this](float value) {
                        int idx = std::clamp(
                            static_cast<int>(std::round(value)), 0, 11);
                        if (snap_label_node_) {
                          if (auto label = snap_label_node_->Get<Label>()) {
                            label->SetText(kSnapNames[idx]);
                          }
                        }
                      },
                      [](Layout& layout) { layout.SetWidth(60.0f); }),
                  Button::TextButton(
                      "+",
                      [this]() {
                        if (notation_view_) notation_view_->ZoomIn();
                      },
                      [](Layout& layout) { layout.SetWidth(26.0f); }),
                  Button::TextButton(
                      "-",
                      [this]() {
                        if (notation_view_) notation_view_->ZoomOut();
                      },
                      [](Layout& layout) { layout.SetWidth(26.0f); }),
                  [&]() {
                    static auto stage_img =
                        Image::LoadImage("/Applications/MusicBox/stage.svg");
                    return ImageButton::BasicImageButton(
                        [this]() { ToggleEnvironmentWindow(); }, stage_img,
                        Tooltip::ShowTooltip("Acoustic Environment (Reverb)"),
                        [](Layout& layout) {
                          layout.SetWidth(28.0f);
                          layout.SetHeight(28.0f);
                        },
                        &stage_button_node_);
                  }(),
                  [&]() {
                    static auto settings_img =
                        Image::LoadImage("/Applications/MusicBox/settings.svg");
                    return ImageButton::BasicImageButton(
                        [this]() { ToggleNotationSettingsWindow(); },
                        settings_img, Tooltip::ShowTooltip("Notation Settings"),
                        [](Layout& layout) {
                          layout.SetWidth(28.0f);
                          layout.SetHeight(28.0f);
                        },
                        &settings_button_node_);
                  }())),

          // --- Song Timeline Ruler Bar ---
          timeline_ruler_->GetNode(),

          // --- Workspace Main Container (Resizable Splitter when tracks
          // visible, direct notation+keyboard when hidden) ---
          [&]() {
            auto left_sidebar = Container::VerticalContainer(
                [](ResizableContainerItem& item) {
                  item.SetBehavior(ResizableContainerItem::Behavior::Fixed);
                },
                [](Layout& layout) {
                  layout.SetWidth(255.0f);
                  layout.SetAlignSelf(YGAlignStretch);
                },
                tracks_panel_->GetNode(), &left_sidebar_node_);

            right_content_node_ = Container::VerticalContainer(
                [](ResizableContainerItem& item) {
                  item.SetBehavior(ResizableContainerItem::Behavior::Flex);
                },
                [](Layout& layout) {
                  layout.SetAlignSelf(YGAlignStretch);
                  layout.SetWidthPercent(100.0f);
                  layout.SetFlexGrow(1.0f);
                  layout.SetFlexShrink(1.0f);
                  layout.SetMinHeight(100.0f);
                  layout.SetGap(0.0f);
                },
                Container::VerticalContainer(
                    [](Layout& layout) {
                      layout.SetAlignSelf(YGAlignStretch);
                      layout.SetWidthPercent(100.0f);
                      layout.SetFlexGrow(1.0f);
                      layout.SetFlexShrink(1.0f);
                      layout.SetMinHeight(100.0f);
                    },
                    &notation_container_node_),
                keyboard_->GetNode());

            resizable_container_node_ = ResizableContainer::HorizontalContainer(
                [](Layout& layout) {
                  layout.SetAlignSelf(YGAlignStretch);
                  layout.SetWidthPercent(100.0f);
                  layout.SetFlexGrow(1.0f);
                  layout.SetFlexShrink(1.0f);
                  layout.SetMinHeight(100.0f);
                },
                left_sidebar, right_content_node_);

            return Container::VerticalContainer(
                [](Layout& layout) {
                  layout.SetAlignSelf(YGAlignStretch);
                  layout.SetWidthPercent(100.0f);
                  layout.SetFlexGrow(1.0f);
                  layout.SetFlexShrink(1.0f);
                  layout.SetMinHeight(100.0f);
                },
                resizable_container_node_, &workspace_container_node_);
          }()));

  // Hook keyboard event callbacks on main focusable component
  main_focusable_comp->OnKeyDown(
      [this](const perception::window::KeyboardKeyEvent& event) {
        HandleKeyDown(event);
      });
  main_focusable_comp->OnKeyUp(
      [this](const perception::window::KeyboardKeyEvent& event) {
        HandleKeyUp(event);
      });

  main_focusable_comp->Focus();

  // Instantiate default notation mode (Falling Notes)
  SetNotationMode(NotationType::FallingNotes);

  // Populate initial active track quick bar UI
  UpdateActiveTrackQuickBarUI();
}

void MusicBoxWindow::SetNotationMode(NotationType type) {
  current_notation_type_ = type;
  notation_view_ = CreateNotationView(type, track_manager_);
  if (notation_view_) {
    notation_view_->SetUndoManager(&undo_manager_);
    notation_view_->SetOnTrackSelectedCallback(
        [this](int track_id, bool auto_scroll) {
          UpdateActiveTrackQuickBarUI();
          if (tracks_panel_) tracks_panel_->UpdateTrackListUI();
          if (notation_view_)
            notation_view_->OnTrackSelected(track_id, auto_scroll);
        });
  }
  if (notation_view_ && notation_view_->GetNode()) {
    auto n_focusable = notation_view_->GetNode()->GetOrAdd<Focusable>();
    n_focusable->OnKeyDown(
        [this](const perception::window::KeyboardKeyEvent& event) {
          HandleKeyDown(event);
        });
    n_focusable->OnKeyUp(
        [this](const perception::window::KeyboardKeyEvent& event) {
          HandleKeyUp(event);
        });
  }

  if (notation_view_) {
    notation_view_->SetBeatsPerBar(current_song_metadata_.beats_per_bar);
    notation_view_->SetNotePerBeat(current_song_metadata_.note_per_beat);
    notation_view_->OnTrackSelected(track_manager_.GetActiveTrackId(),
                                    /*auto_scroll=*/true);
  }
  if (notation_container_node_) {
    notation_container_node_->RemoveChildren();
    if (notation_view_) {
      notation_container_node_->AddChild(notation_view_->GetNode());
    }
  }
}

void MusicBoxWindow::ToggleTracksPanelVisibility() {
  is_tracks_panel_visible_ = !is_tracks_panel_visible_;

  if (workspace_container_node_ && resizable_container_node_ &&
      right_content_node_) {
    if (is_tracks_panel_visible_) {
      workspace_container_node_->RemoveChild(right_content_node_);
      resizable_container_node_->AddChild(right_content_node_);
      workspace_container_node_->AddChild(resizable_container_node_);
    } else {
      resizable_container_node_->RemoveChild(right_content_node_);
      workspace_container_node_->RemoveChild(resizable_container_node_);
      workspace_container_node_->AddChild(right_content_node_);
    }
    workspace_container_node_->Invalidate();
  }

  if (tracks_toggle_button_node_) {
    if (auto button = tracks_toggle_button_node_->Get<Button>()) {
      button->SetToggled(is_tracks_panel_visible_);
    }
  }

  if (notation_view_) notation_view_->Invalidate();
}

void MusicBoxWindow::ToggleNotationSettingsWindow() {
  if (notation_settings_window_) {
    notation_settings_window_->Focus();
    return;
  }

  notation_settings_window_ = std::make_unique<NotationSettingsWindow>(
      current_song_metadata_,
      [this]() {
        int old_bar = notation_view_ ? notation_view_->GetBeatsPerBar()
                                     : current_song_metadata_.beats_per_bar;
        SongSettingsState old_s{
            .bpm = track_manager_.GetBpm(),
            .beats_per_bar = old_bar,
            .note_per_beat = current_song_metadata_.note_per_beat};
        SongSettingsState new_s{
            .bpm = track_manager_.GetBpm(),
            .beats_per_bar = current_song_metadata_.beats_per_bar,
            .note_per_beat = current_song_metadata_.note_per_beat};
        undo_manager_.PushAction(
            std::make_unique<ChangeSongSettingsUndoAction>(old_s, new_s));
        if (notation_view_) {
          notation_view_->SetBeatsPerBar(current_song_metadata_.beats_per_bar);
        }
        UpdateUndoButtonUI();
      },
      [this]() {
        int old_npb = notation_view_ ? notation_view_->GetNotePerBeat()
                                     : current_song_metadata_.note_per_beat;
        SongSettingsState old_s{
            .bpm = track_manager_.GetBpm(),
            .beats_per_bar = current_song_metadata_.beats_per_bar,
            .note_per_beat = old_npb};
        SongSettingsState new_s{
            .bpm = track_manager_.GetBpm(),
            .beats_per_bar = current_song_metadata_.beats_per_bar,
            .note_per_beat = current_song_metadata_.note_per_beat};
        undo_manager_.PushAction(
            std::make_unique<ChangeSongSettingsUndoAction>(old_s, new_s));
        if (notation_view_) {
          notation_view_->SetNotePerBeat(current_song_metadata_.note_per_beat);
          if (current_notation_type_ == NotationType::Staff) {
            notation_view_->Invalidate();
          }
        }
        UpdateUndoButtonUI();
      },
      [this]() { notation_settings_window_.reset(); });
}

void MusicBoxWindow::ToggleKeyboardVisibility() {
  is_keyboard_visible_ = !is_keyboard_visible_;

  if (keyboard_ && keyboard_->GetNode()) {
    keyboard_->GetNode()->GetLayout().SetDisplay(
        is_keyboard_visible_ ? YGDisplayFlex : YGDisplayNone);
    keyboard_->GetNode()->Invalidate();
  }

  if (keyboard_button_node_) {
    if (auto button = keyboard_button_node_->Get<Button>()) {
      button->SetToggled(is_keyboard_visible_);
    }
  }

  if (notation_view_) notation_view_->Invalidate();
}

void MusicBoxWindow::UpdateActiveTrackQuickBarUI() {
  Track* active = track_manager_.GetActiveTrack();
  if (!active) return;

  if (active_track_label_node_) {
    if (auto lbl = active_track_label_node_->Get<Label>()) {
      lbl->SetText(active->name);
      lbl->SetColor(active->color);
    }
  }

  if (active_track_instrument_combo_node_) {
    if (auto combo = active_track_instrument_combo_node_->Get<ComboBox>()) {
      int selected_idx = GetOptionIndexForInstrument(active->instrument);
      combo->SetSelection(selected_idx);
    }
  }
}

void MusicBoxWindow::ToggleEnvironmentWindow() {
  if (environment_window_) {
    environment_window_->Close();
    environment_window_.reset();
    return;
  }

  environment_window_ = std::make_unique<EnvironmentWindow>(
      [this]() {
        if (notation_view_) notation_view_->Invalidate();
      },
      [this]() { environment_window_.reset(); });
}

void MusicBoxWindow::ShowHelpWindow() {
  if (help_window_) {
    help_window_->Focus();
    return;
  }

  help_window_ =
      std::make_unique<HelpWindow>([this]() { help_window_.reset(); });
}

void MusicBoxWindow::ToggleMetronome() {
  metronome_.SetEnabled(!metronome_.IsEnabled());
  if (metronome_button_node_) {
    if (auto btn = metronome_button_node_->Get<Button>()) {
      btn->SetToggled(metronome_.IsEnabled());
    }
  }
}

void MusicBoxWindow::UpdateTransportButtonsUI() {
  if (track_manager_.IsRecording()) {
    if (play_button_node_) {
      if (auto btn = play_button_node_->Get<Button>()) {
        btn->SetToggled(false);
        btn->SetButtonStyle(Button::ButtonStyle::DISABLED);
      }
    }
    if (record_button_node_) {
      if (auto btn = record_button_node_->Get<Button>()) {
        btn->SetButtonStyle(Button::ButtonStyle::DEFAULT);
        btn->SetLabelColor(0xFFEF4444);
        btn->SetToggled(true);
      }
    }
  } else if (track_manager_.IsPlaying()) {
    if (play_button_node_) {
      if (auto btn = play_button_node_->Get<Button>()) {
        btn->SetButtonStyle(Button::ButtonStyle::DEFAULT);
        btn->SetToggled(true);
      }
    }
    if (record_button_node_) {
      if (auto btn = record_button_node_->Get<Button>()) {
        btn->SetToggled(false);
        btn->SetButtonStyle(Button::ButtonStyle::DISABLED);
      }
    }
  } else {
    if (play_button_node_) {
      if (auto btn = play_button_node_->Get<Button>()) {
        btn->SetButtonStyle(Button::ButtonStyle::DEFAULT);
        btn->SetToggled(false);
      }
    }
    if (record_button_node_) {
      if (auto btn = record_button_node_->Get<Button>()) {
        btn->SetButtonStyle(Button::ButtonStyle::DEFAULT);
        btn->SetLabelColor(0xFFEF4444);
        btn->SetToggled(false);
      }
    }
  }

  if (solo_button_node_) {
    if (auto btn = solo_button_node_->Get<Button>()) {
      btn->SetToggled(track_manager_.IsGlobalSoloActive());
    }
  }

  int current_time_ms = track_manager_.GetCurrentTimeMs();
  int duration_ms = track_manager_.GetSongDurationMs();

  bool at_start = (current_time_ms <= 0);
  bool at_end = (current_time_ms >= duration_ms);

  if (rewind_button_node_) {
    if (auto btn = rewind_button_node_->Get<Button>()) {
      btn->SetButtonStyle(at_start ? Button::ButtonStyle::DISABLED
                                   : Button::ButtonStyle::DEFAULT);
    }
  }

  if (fast_forward_button_node_) {
    if (auto btn = fast_forward_button_node_->Get<Button>()) {
      btn->SetButtonStyle(at_end ? Button::ButtonStyle::DISABLED
                                 : Button::ButtonStyle::DEFAULT);
    }
  }
}

void MusicBoxWindow::PerformUndo() {
  if (!undo_manager_.CanUndo()) return;
  is_undoing_ = true;
  undo_manager_.Undo(track_manager_, current_song_metadata_,
                     notation_view_.get());
  if (tracks_panel_) tracks_panel_->UpdateTrackListUI();
  UpdateActiveTrackQuickBarUI();
  if (bpm_input_node_) {
    if (auto input_box = bpm_input_node_->Get<InputBox>()) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%.0f", current_song_metadata_.bpm);
      input_box->SetText(buf);
    }
  }
  if (notation_settings_window_) {
    notation_settings_window_->UpdateValues(current_song_metadata_);
  }
  if (notation_view_) {
    notation_view_->SetBeatsPerBar(current_song_metadata_.beats_per_bar);
    notation_view_->SetNotePerBeat(current_song_metadata_.note_per_beat);
    notation_view_->OnTrackSelected(track_manager_.GetActiveTrackId(),
                                    /*auto_scroll=*/true);
    notation_view_->Invalidate();
  }
  UpdateTransportButtonsUI();
  UpdateUndoButtonUI();
  is_undoing_ = false;
}

void MusicBoxWindow::UpdateUndoButtonUI() {
  if (undo_button_node_) {
    if (auto btn = undo_button_node_->Get<Button>()) {
      btn->SetButtonStyle(undo_manager_.CanUndo()
                              ? Button::ButtonStyle::DEFAULT
                              : Button::ButtonStyle::DISABLED);
    }
    undo_button_node_->Invalidate();
  }
}

void MusicBoxWindow::TriggerNoteOn(int key_index) {
  if (!keyboard_ || !keyboard_->IsKeyPressed(key_index)) {
    if (keyboard_) keyboard_->SetKeyPressed(key_index, true);

    if (notation_view_) notation_view_->Invalidate();

    track_manager_.OnNotePressed(key_index);

    const Instrument* inst = GetDefaultInstrument();
    float vol = 0.8f;
    Track* active_track = track_manager_.GetActiveTrack();
    if (active_track) {
      inst = active_track->instrument;
      vol = active_track->volume;
    }

    perception::Fiber::Create([key_index, inst, vol]() {
      NoteOn(key_index, inst, vol);
    })->WakeUp();
  }
}

void MusicBoxWindow::TriggerNoteOff(int key_index) {
  if (keyboard_ && keyboard_->IsKeyPressed(key_index)) {
    keyboard_->SetKeyPressed(key_index, false);

    if (notation_view_) notation_view_->Invalidate();

    track_manager_.OnNoteReleased(key_index);

    perception::Fiber::Create([key_index]() { NoteOff(key_index); })->WakeUp();
  }
}

void MusicBoxWindow::ShiftTopOctave(int delta) {
  int new_octave = std::clamp(top_octave_ + delta, 1, 7);
  if (new_octave != top_octave_) {
    top_octave_ = new_octave;
    if (keyboard_) {
      keyboard_->SetOctaves(bottom_octave_, top_octave_);
    }
  }
}

void MusicBoxWindow::ShiftBottomOctave(int delta) {
  int new_octave = std::clamp(bottom_octave_ + delta, 1, 7);
  if (new_octave != bottom_octave_) {
    bottom_octave_ = new_octave;
    if (keyboard_) {
      keyboard_->SetOctaves(bottom_octave_, top_octave_);
    }
  }
}

void MusicBoxWindow::HandleKeyDown(
    const perception::window::KeyboardKeyEvent& event) {
  if (event.key ==
          static_cast<unsigned char>(perception::ui::KeyCode::Delete) ||
      event.key ==
          static_cast<unsigned char>(perception::ui::KeyCode::Backspace)) {
    if (notation_view_) {
      if (notation_view_->DeleteHoveredNote()) {
        return;
      }
    }
  }

  if (perception::ui::IsControlKey(event.key)) {
    if (notation_view_) notation_view_->SetControlDown(true);
  }

  bool is_input_focused = false;
  if (window_) {
    if (auto ui_win = window_->Get<UiWindow>()) {
      if (auto focused = ui_win->GetFocusedNode()) {
        if (focused->Get<InputBox>()) {
          is_input_focused = true;
        }
      }
    }
  }

  if (!is_input_focused) {
    if (event.key == static_cast<unsigned char>(perception::ui::KeyCode::Z) &&
        (perception::ui::IsControlKey(event.key) ||
         (notation_view_ && notation_view_->IsControlDown()))) {
      PerformUndo();
      return;
    }
    if (event.key ==
        static_cast<unsigned char>(perception::ui::KeyCode::LeftBracket)) {
      ShiftTopOctave(-1);
      return;
    }
    if (event.key ==
        static_cast<unsigned char>(perception::ui::KeyCode::RightBracket)) {
      ShiftTopOctave(1);
      return;
    }
    if (event.key ==
        static_cast<unsigned char>(perception::ui::KeyCode::Comma)) {
      ShiftBottomOctave(-1);
      return;
    }
    if (event.key ==
        static_cast<unsigned char>(perception::ui::KeyCode::Period)) {
      ShiftBottomOctave(1);
      return;
    }
    if (event.key ==
        static_cast<unsigned char>(perception::ui::KeyCode::Space)) {
      if (notation_view_) notation_view_->SetSpaceDown(true);
    }
  }

  if (keyboard_) {
    std::optional<int> key_idx =
        keyboard_->MapKeyCodeToKeyIndex(static_cast<KeyCode>(event.key));
    if (key_idx.has_value()) {
      TriggerNoteOn(*key_idx);
    }
  }
}

void MusicBoxWindow::HandleKeyUp(
    const perception::window::KeyboardKeyEvent& event) {
  if (perception::ui::IsControlKey(event.key)) {
    if (notation_view_) notation_view_->SetControlDown(false);
  }
  if (event.key == static_cast<unsigned char>(perception::ui::KeyCode::Space)) {
    if (notation_view_) notation_view_->SetSpaceDown(false);
  }

  if (keyboard_) {
    std::optional<int> key_idx =
        keyboard_->MapKeyCodeToKeyIndex(static_cast<KeyCode>(event.key));
    if (key_idx.has_value()) {
      TriggerNoteOff(*key_idx);
    }
  }
}

void MusicBoxWindow::OnTimerTick() {
  while (window_open_) {
    auto now = std::chrono::steady_clock::now();
    int elapsed_ms = 30;
    if (first_tick_) {
      first_tick_ = false;
    } else {
      elapsed_ms = static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                last_tick_time_)
              .count());
      elapsed_ms = std::clamp(elapsed_ms, 10, 100);
    }
    last_tick_time_ = now;

    track_manager_.Tick(elapsed_ms, [](int key_index, const Instrument* inst,
                                       float vol, float duration_seconds) {
      PlayNote(key_index, inst, vol, duration_seconds);
    });

    metronome_.Tick(elapsed_ms,
                    track_manager_.IsPlaying() || track_manager_.IsRecording(),
                    track_manager_.GetCurrentTimeMs(), track_manager_.GetBpm(),
                    current_song_metadata_.beats_per_bar);

    UpdateTransportButtonsUI();

    if (notation_view_) notation_view_->Invalidate();
    if (keyboard_) keyboard_->Invalidate();
    if (timeline_ruler_) timeline_ruler_->Invalidate();

    perception::SleepForDuration(std::chrono::milliseconds(30));
  }
}

}  // namespace windows
