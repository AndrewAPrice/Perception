{
  package_type: 'library',
  include_directories: [
    'source',
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
