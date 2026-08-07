{
  skip_for_tests: true,
  dependencies+: [
    'musl',
    'libcxx',
    'libsdl-org SDL',
    'mesa',
  ],
  source_directories: [
    'source',
  ],
  asset_directories: [
    'assets',
  ],
}
