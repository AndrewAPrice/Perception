{
  package_type: 'library',
  skip_for_tests: true,
  include_directories: [
    'source',
    '../../third_party/Libraries/musl/include',
  ],
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
