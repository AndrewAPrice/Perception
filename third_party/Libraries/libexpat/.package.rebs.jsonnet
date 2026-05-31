{
  package_type: 'library',
  public_include_directories: [
    'public',
  ],
  defines+: [
    'XML_GE',
  ],
  source_directories: [
    'source',
  ],
  files_to_ignore: [
    'source/xcsinc.c',
  ],
}
