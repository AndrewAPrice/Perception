{
  skip_for_tests: true,
  package_type: 'library',
  dependencies+: [
    'libwapcaplet',
    'libdom',
  ],
  public_include_directories: [
    'public',
  ],
  include_directories: [
    'source',
  ],
  source_directories: [
    'source',
  ],
}
