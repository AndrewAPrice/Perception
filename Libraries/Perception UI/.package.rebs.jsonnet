{
  package_type: 'library',
  public_include_directories: [
    'public',
  ],
  source_directories: [
    'source',
  ],
  dependencies+: [
    'facebook yoga',
    'google skia',
    'perception',
    'Perception Window',
  ],
}
