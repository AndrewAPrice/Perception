{
  skip_for_tests: true,
  package_type: 'library',
  dependencies+: [
    'musl',
    'libclang compiler headers',
  ],
  public_include_directories: [
    'public',
  ],
  include_directories: [
    'source',
  ],
  defines+: [
    '_GNU_SOURCE',
    '__linux__',
    'asm=__asm__',
    'CONFIG_VERSION="\\"2026-06-04\\""',
  ],
  source_directories: [
    'source',
  ],
}
