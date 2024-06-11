{
  package_type: 'library',
  dependencies+: [
    'Linux System Call Shim',
  ],
  include_priority: 2,
  public_include_directories: [
    'include',
  ],
  include_directories: [
    'source',
    'source/include',
    'source/internal',
  ],
  source_directories: [
    'source',
  ],
  defines+: [
    '_XOPEN_SOURCE=700',
    '-_GNU_SOURCE',
  ],
  public_defines: [
    '_GNU_SOURCE',
  ],
}
