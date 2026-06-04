{
  skip_for_tests: true,
  package_type: 'library',
  dependencies+: [
    'libparserutils',
    'libwapcaplet',
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
