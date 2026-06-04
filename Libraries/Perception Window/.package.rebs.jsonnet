{
  skip_for_tests: true,
  package_type: 'library',
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
