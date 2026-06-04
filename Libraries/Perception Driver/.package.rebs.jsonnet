{
  package_type: 'library',
  skip_for_tests: true,
  public_include_directories: [
    'public',
  ],
  source_directories: [
    'source',
  ],
  dependencies+: [
    'perception',
  ],
}
