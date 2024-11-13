{
  package_type: 'library',
  public_include_directories: [
    'public',
  ],
  source_directories: [
    'source',
  ],
  files_to_ignore: [
      "source/testparser.c",
      "source/runtest.c",
      "source/runxmlconf.c",
      "source/testModule.c",
      "source/xmlcatalog.c",
      "source/runsuite.c",
      "source/xmllint.c"
  ],
  dependencies+: [
    'madler zlib',
    'unicode-org icu',
  ],
}
