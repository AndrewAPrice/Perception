{
  package_type: 'library',
  public_include_directories: [
    'public',
  ],
  source_directories: [
    'source',
  ],
  dependencies: [
    'perception',
  ],
} + (if is_testing then {
  files_to_ignore: [
    'source/perception/window/perception_window.cc',
  ],
} else {})
