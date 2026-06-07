{
  skip_for_tests: true,
  asset_directories: [
    'assets',
  ],
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
