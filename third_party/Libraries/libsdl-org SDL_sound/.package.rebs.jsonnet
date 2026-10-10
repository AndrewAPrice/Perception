{
  skip_for_tests: true,
  package_type: 'library',
  defines+: [
    'SDL_DYNAMIC_API=0',
  ],
  dependencies+: [
    'musl',
    'libsdl-org SDL',
  ],
  public_include_directories: [
    'public',
  ],
  include_directories: [
    'source',
    'public',
  ],
  source_directories: [
    'source',
  ],
}
