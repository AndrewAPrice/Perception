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
    'source/music_player_window.cc',
  ],
} else {
  skip_for_tests: true,
})
