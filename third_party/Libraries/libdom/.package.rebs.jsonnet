{
  skip_for_tests: true,
  package_type: 'library',
  dependencies+: [
    'libparserutils',
    'libwapcaplet',
    'libhubbub',
    'gnome libxml2',
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
  files_to_ignore: [
    'source/bindings/xml/expat_xmlparser.c',
  ],
}
