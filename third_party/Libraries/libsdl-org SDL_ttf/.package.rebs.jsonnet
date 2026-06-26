{
  skip_for_tests: true,
  package_type: 'library',
  defines+: [
    'SDL_DYNAMIC_API=0',
  ],
  dependencies+: [
    'musl',
    'libsdl-org SDL',
    'freetype',
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
}
