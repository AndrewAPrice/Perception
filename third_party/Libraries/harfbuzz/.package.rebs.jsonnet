{
  skip_for_tests: true,
  package_type: 'library',
  public_include_directories: [
    'public',
    'public/harfbuzz',
  ],
  defines+: [
    'HAVE_FREETYPE',
    'HAVE_ICU',
  ],
  source_directories: [
    'source',
    'public/harfbuzz/graph',
    'public/harfbuzz/OT/Var/VARC'
  ],
  files_to_ignore: [
    'source/harfbuzz-world.cc',
  ],
  dependencies+: [
    'freetype',
    'unicode-org icu',
  ],
}
