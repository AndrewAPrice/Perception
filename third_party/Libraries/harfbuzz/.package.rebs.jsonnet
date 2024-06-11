{
  package_type: 'library',
  public_include_directories: [
    'public',
  ],
  defines+: [
    'HAVE_FREETYPE',
    'HAVE_ICU',
  ],
  source_directories: [
    'source',
  ],
  dependencies+: [
    'freetype',
    'unicode-org icu',
  ],
}
