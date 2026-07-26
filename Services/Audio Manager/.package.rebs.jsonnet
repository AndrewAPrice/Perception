{
  dependencies+: [
    'perception',
  ],
  include_directories: [
    'source',
  ],
  source_directories: [
    'source',
  ],
} + (if is_testing then {
  files_to_ignore: [
    'source/main.cc',
  ],
} else {
  skip_for_tests: true,
})
