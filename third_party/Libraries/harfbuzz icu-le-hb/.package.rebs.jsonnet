{
  package_type: 'library',
  public_include_directories: [
    'public',
  ],
  source_directories: [
    'source',
  ],
  dependencies+: [
    'harfbuzz',
    'unicode-org icu',
  ],
}
