{
  skip_for_tests: true,
  package_type: 'library',
  public_include_directories: [
    'public',
  ],
  include_directories: [
    'source',
  ],
  dependencies+: [
    'madler zlib',
  ],
}
