{
  dependencies+: [
    'perception',
    'Perception UI',
    'Perception Window',
    'richgel999 fpng',
  ],
  source_directories: [
    'source',
  ],
  asset_directories: [
    'assets',
  ],
} + (if is_testing then {
  files_to_ignore: [
    'source/main.cc',
  ],
} else {
  skip_for_tests: true,
})

