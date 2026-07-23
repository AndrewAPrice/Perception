{
  skip_for_tests: true,
  package_type: 'library',
  dependencies+: [
    'musl',
  ],
  public_include_directories: [
    'public',
  ],
  defines+: [
    'SQLITE_THREADSAFE=1',
    'SQLITE_OMIT_LOAD_EXTENSION',
  ],
  source_directories: [
    'source',
  ],
}
