{
  skip_for_tests: true,
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
    'public/graph',
    'public/OT/Var/VARC'
  ],
  files_to_ignore: [
    'source/harfbuzz-world.cc',
  ],
  dependencies+: [
    'freetype',
    'unicode-org icu',
  ],
}
