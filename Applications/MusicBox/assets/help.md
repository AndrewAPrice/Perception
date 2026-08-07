MusicBox is a digital audio workstation and composition tool for Perception.

## Overview
* **File menu**: Create, load, save songs, export audio, or access Help.
* **Undo**: Revert recent actions including recording, track changes, note edits, or setting changes.
* **Toolbar toggles**: Toggle left Tracks panel or bottom Piano Keyboard.
* **Transport Bar**: Playback, recording, navigation, and solo controls.
* **BPM**: Sets how many beats per minute, which affects recording and play speed.
* **Metronome**: Plays audio ticks for each beat, with a major tick for bars.
* **View & Tools**: Switch Notation mode, adjust Beat Snap, Zoom in/out (+/-), open Acoustic Environment (Stage), or Notation Settings (Gear).

## Playback, recording, and timeline controls
* **Jump to start of song** (|◀): Seeks playback position to 0 ms.
* **Jump to end of song** (▶|): Seeks playback position to the end of the song.
* **Play** (▶): Toggleable song playback. Cannot be started while Recording is active.
* **Record** (●): Toggleable live recording into the active track. Cannot be started while Play is active. (Play and Record cannot be active simultaneously).
* **Solo** (S): Toggleable solo mode. When Solo is enabled, only the currently selected track is played.

## Composing Music
* Click on the composition canvas to insert a new note.
* Drag notes horizontally to adjust start time, or vertically to adjust pitch.
* Drag the top or bottom edge of a note to resize its duration.
* Hover over a note and press **Delete** or **Backspace** to delete it.
* Hold **Ctrl** to switch to Eraser mode and click a note to delete it.
* **Beat Snapping**: Adjust the Snap setting in the toolbar (e.g. 1/4, 1/8, 1/16). When placing, moving, or resizing notes, Beat Snapping controls the granularity that start times and durations will be snapped to.
* Hold **Space** + Drag mouse to pan across the composition canvas.
* Use the **Zoom In** (+) and **Zoom Out** (-) buttons in the toolbar to adjust view scale.

## Notation Views
* **Falling Notes View**: A visual waterfall display showing notes cascading toward the performance line.
* **Staff View**: Traditional musical stave layout showing notes on treble and bass clefs.
* Switch between notation modes anytime in the toolbar dropdown without losing note data.

## Tracks & Instruments
* Click **+ Add Track** in the left panel to create a new track.
* Select a track to edit its notes and assign synthesized or sampled instruments.
* Choose from over 30 built-in instruments (Piano, Synths, Strings, Guitars, Organ, Percussion, etc.).
* **Mute** (M) or **Solo** (S) tracks, and adjust track Volume, Pan, and Reverb send.

## Playing via Keyboard & Live Recording
* Click keys on the bottom interactive piano keyboard to trigger notes.
* Use your computer's QWERTY keyboard to play notes live across top and bottom octaves.
* Use `[` and `]` to shift the top octave down or up.
* Use `,` and `.` to shift the bottom octave down or up.
* Click **Record** (●) to capture live keyboard performance directly into the active track.

## Acoustic Environment (Reverb)
* Click the **Stage** icon in the toolbar to open the Acoustic Environment window.
* **Enable Acoustic Reverb**: Checkbox to toggle global acoustic reverb processing.
* **Presets**: Select pre-configured acoustic room profiles (Studio Room, Concert Hall, Cathedral).
* **Room Size**: Slider (0.0 to 1.0) to adjust the simulated spatial room dimensions.
* **Reverb Mix**: Slider (0.0 to 1.0) to set the wet/dry audio mix of the reverb effect.

## Notation Settings
* Click the **Gear** icon in the toolbar to open the Notation Settings window.
* **Beats per bar**: Specifies how many beats per bar. This affects both drawing of the music and how often the metronome does a major tick.
* **Note per beat**: Slider to select what note represents 1 beat in the sheet view. For example, 1/4 means one beat will be represented by a quarter note.

## Keyboard Shortcuts & Modifiers
* `Ctrl + Z`: Undo last action
* `[` / `]`: Shift top octave down / up
* `,` / `.`: Shift bottom octave down / up
* `Delete` / `Backspace`: Delete hovered note
* `Ctrl` (held): Eraser cursor mode (click note to delete)
* `Space` + Mouse Drag: Pan composition view

## Saving & Exporting
* **File -> New Song**: Clear current composition and start fresh.
* **File -> Load Song**: Open a saved `.song` file.
* **File -> Save Song**: Save current composition to a `.song` file.
* **File -> Export as WAV...**: Render composition to a `.wav` audio file.
