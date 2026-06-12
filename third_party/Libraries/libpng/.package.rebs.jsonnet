{
  skip_for_tests: true,
  package_type: 'library',
  public_include_directories: [
    'public',
  ],
  dependencies+: [
    'madler zlib',
  ],
  source_directories: [
    'source',
  ],
}
