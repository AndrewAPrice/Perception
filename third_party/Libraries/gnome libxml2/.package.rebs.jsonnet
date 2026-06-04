{
  skip_for_tests: true,
  package_type: 'library',
  public_include_directories: [
    'public',
  ],
  source_directories: [
    'source',
  ],
  defines+: [
    'XML_SYSCONFDIR=\\""/Libraries/gnome libxml2/assets"\\"'
  ],
  files_to_ignore: [
      "source/testparser.c",
      "source/runtest.c",
      "source/runxmlconf.c",
      "source/testModule.c",
      "source/xmlcatalog.c",
      "source/runsuite.c",
      "source/xmllint.c",
      "source/lintmain.c"
  ],
  dependencies+: [
    'madler zlib',
    'unicode-org icu',
  ],
}
