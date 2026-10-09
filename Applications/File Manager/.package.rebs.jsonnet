{
  dependencies+: [
    'perception',
    'Perception UI',
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
    'source/dialogs.cc',
    'source/file_list_view.cc',
    'source/file_manager_window.cc',
  ],
} else {
  skip_for_tests: true,
})
