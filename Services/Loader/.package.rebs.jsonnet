{
  skip_for_tests: true,
  dependencies+: [
    'elf',
    'perception',
    'Perception Driver',
  ],
  source_directories: [
    'source',
  ],
  statically_link: 1
}
