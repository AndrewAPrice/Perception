{
  dependencies+: [
    'perception',
    'Perception UI',
    'google skia',
  ],
  include_directories: [
    'source',
  ],
  source_directories: [
    'source',
  ],
  asset_directories: [
    'assets',
  ],
} + (if is_testing then {
  dependencies+: [
    'Perception Test',
  ],
  files_to_ignore: [
    'source/main.cc',
    'source/music_box_window.cc',
    'source/notation/falling_notes_view.cc',
    'source/notation/notation_view.cc',
    'source/notation/sheet_view.cc',
    'source/synth_engine.cc',
    'source/notation/notation_drawing.cc',
    'source/notation/sheet_view_test.cc',
    'source/track_manager_test.cc',
    'source/song_serializer.cc',
    'source/panels/keyboard.cc',
    'source/panels/timeline_ruler.cc',
    'source/windows/music_box_window.cc',
    'source/panels/tracks_panel.cc',
    'source/windows/environment_window.cc',
    'source/windows/notation_settings_window.cc',
  ],
} else {
  skip_for_tests: true,
})
