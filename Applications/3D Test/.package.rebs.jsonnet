{
  package_type: 'application',
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
