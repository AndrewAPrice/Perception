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

#include "windows/help_window.h"

#include "perception/ui/components/container.h"
#include "perception/ui/components/group_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"

using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Container;
using ::perception::ui::components::GroupBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::UiWindow;

namespace windows {
namespace {

// Default width for the Help window.
constexpr float kHelpWindowWidth = 560.0f;

// Default height for the Help window.
constexpr float kHelpWindowHeight = 480.0f;

// Spacing between sections in the Help window content container.
constexpr float kSectionGap = 8.0f;

}  // namespace

HelpWindow::HelpWindow(std::function<void()> on_closed)
    : on_closed_(std::move(on_closed)) {
  BuildUI();
}

void HelpWindow::Focus() {
  if (!window_node_) return;
  if (auto ui_win = window_node_->Get<UiWindow>()) ui_win->Focus();
}

void HelpWindow::Close() {
  if (!window_node_) return;
  if (auto ui_win = window_node_->Get<UiWindow>()) ui_win->Close();
}

void HelpWindow::BuildUI() {
  auto content = Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetGap(kSectionGap);
      },

      // Overview Section
      GroupBox::VerticalGroupBox(
          "Overview",
          Label::BasicLabel("MusicBox is a digital audio workstation and composition tool for Perception."),
          Label::BasicLabel("• File menu: Create, load, save songs, export audio, or access Help."),
          Label::BasicLabel("• Undo: Revert recent actions including recording, track changes, note edits, or setting changes."),
          Label::BasicLabel("• Toolbar toggles: Toggle left Tracks panel or bottom Piano Keyboard."),
          Label::BasicLabel("• Transport Bar: Playback, recording, navigation, and solo controls."),
          Label::BasicLabel("• BPM: Sets how many beats per minute, which affects recording and play speed."),
          Label::BasicLabel("• Metronome: Plays audio ticks for each beat, with a major tick for bars."),
          Label::BasicLabel("• View & Tools: Switch Notation mode, adjust Beat Snap, Zoom in/out (+/-), open Acoustic Environment (Stage), or Notation Settings (Gear).")),

      // Playback, recording, and timeline controls Section
      GroupBox::VerticalGroupBox(
          "Playback, recording, and timeline controls",
          Label::BasicLabel("• Jump to start of song (|◀): Seeks playback position to 0 ms."),
          Label::BasicLabel("• Jump to end of song (▶|): Seeks playback position to the end of the song."),
          Label::BasicLabel("• Play (▶): Toggleable song playback. Cannot be started while Recording is active."),
          Label::BasicLabel("• Record (●): Toggleable live recording into the active track. Cannot be started while Play is active. (Play and Record cannot be active simultaneously)."),
          Label::BasicLabel("• Solo (S): Toggleable solo mode. When Solo is enabled, only the currently selected track is played.")),

      // Composing Music
      GroupBox::VerticalGroupBox(
          "Composing Music",
          Label::BasicLabel("• Click on the composition canvas to insert a new note."),
          Label::BasicLabel("• Drag notes horizontally to adjust start time, or vertically to adjust pitch."),
          Label::BasicLabel("• Drag the top or bottom edge of a note to resize its duration."),
          Label::BasicLabel("• Hover over a note and press Delete or Backspace to delete it."),
          Label::BasicLabel("• Hold Ctrl to switch to Eraser mode and click a note to delete it."),
          Label::BasicLabel("• Beat Snapping: Adjust the Snap setting in the toolbar (e.g. 1/4, 1/8, 1/16). When placing, moving, or resizing notes, Beat Snapping controls the granularity that start times and durations will be snapped to."),
          Label::BasicLabel("• Hold Space + Drag mouse to pan across the composition canvas."),
          Label::BasicLabel("• Use the Zoom In (+) and Zoom Out (-) buttons in the toolbar to adjust view scale.")),

      // Notation Views
      GroupBox::VerticalGroupBox(
          "Notation Views",
          Label::BasicLabel("• Falling Notes View: A visual waterfall display showing notes cascading toward the performance line."),
          Label::BasicLabel("• Staff View: Traditional musical stave layout showing notes on treble and bass clefs."),
          Label::BasicLabel("• Switch between notation modes anytime in the toolbar dropdown without losing note data.")),

      // Tracks & Instruments
      GroupBox::VerticalGroupBox(
          "Tracks & Instruments",
          Label::BasicLabel("• Click '+ Add Track' in the left panel to create a new track."),
          Label::BasicLabel("• Select a track to edit its notes and assign synthesized or sampled instruments."),
          Label::BasicLabel("• Choose from over 30 built-in instruments (Piano, Synths, Strings, Guitars, Organ, Percussion, etc.)."),
          Label::BasicLabel("• Mute (M) or Solo (S) tracks, and adjust track Volume, Pan, and Reverb send.")),

      // Live Keyboard & Performance
      GroupBox::VerticalGroupBox(
          "Playing via Keyboard & Live Recording",
          Label::BasicLabel("• Click keys on the bottom interactive piano keyboard to trigger notes."),
          Label::BasicLabel("• Use your computer's QWERTY keyboard to play notes live across top and bottom octaves."),
          Label::BasicLabel("• Use [ and ] to shift the top octave down or up."),
          Label::BasicLabel("• Use , and . to shift the bottom octave down or up."),
          Label::BasicLabel("• Click Record (●) to capture live keyboard performance directly into the active track.")),

      // Acoustic Environment Section
      GroupBox::VerticalGroupBox(
          "Acoustic Environment (Reverb)",
          Label::BasicLabel("• Click the Stage icon in the toolbar to open the Acoustic Environment window."),
          Label::BasicLabel("• Enable Acoustic Reverb: Checkbox to toggle global acoustic reverb processing."),
          Label::BasicLabel("• Presets: Select pre-configured acoustic room profiles (Studio Room, Concert Hall, Cathedral)."),
          Label::BasicLabel("• Room Size: Slider (0.0 to 1.0) to adjust the simulated spatial room dimensions."),
          Label::BasicLabel("• Reverb Mix: Slider (0.0 to 1.0) to set the wet/dry audio mix of the reverb effect.")),

      // Notation Settings Section
      GroupBox::VerticalGroupBox(
          "Notation Settings",
          Label::BasicLabel("• Click the Gear icon in the toolbar to open the Notation Settings window."),
          Label::BasicLabel("• Beats per bar: Specifies how many beats per bar. This affects both drawing of the music and how often the metronome does a major tick."),
          Label::BasicLabel("• Note per beat: Slider to select what note represents 1 beat in the sheet view. For example, 1/4 means one beat will be represented by a quarter note.")),

      // Keyboard Shortcuts
      GroupBox::VerticalGroupBox(
          "Keyboard Shortcuts & Modifiers",
          Label::BasicLabel("• Ctrl + Z: Undo last action"),
          Label::BasicLabel("• [ / ]: Shift top octave down / up"),
          Label::BasicLabel("• , / .: Shift bottom octave down / up"),
          Label::BasicLabel("• Delete / Backspace: Delete hovered note"),
          Label::BasicLabel("• Ctrl (held): Eraser cursor mode (click note to delete)"),
          Label::BasicLabel("• Space + Mouse Drag: Pan composition view")),

      // Saving & Exporting
      GroupBox::VerticalGroupBox(
          "Saving & Exporting",
          Label::BasicLabel("• File -> New Song: Clear current composition and start fresh."),
          Label::BasicLabel("• File -> Load Song: Open a saved .song file."),
          Label::BasicLabel("• File -> Save Song: Save current composition to a .song file."),
          Label::BasicLabel("• File -> Export as WAV...: Render composition to a .wav audio file.")));

  auto scroll_container = ScrollContainer::VerticalScrollContainer(content);

  window_node_ = UiWindow::ResizableWindowWithTitleBar(
      "Welcome to MusicBox",
      [this](UiWindow& window) {
        window.OnClose([this]() {
          window_node_.reset();
          if (on_closed_) on_closed_();
        });
      },
      [](Layout& layout) {
        layout.SetWidth(kHelpWindowWidth);
        layout.SetHeight(kHelpWindowHeight);
      },
      scroll_container);
}

}  // namespace windows
