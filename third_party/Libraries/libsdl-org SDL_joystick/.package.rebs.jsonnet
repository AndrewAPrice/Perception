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
  include_directories: [
    'source',
  ],
  source_directories: [
    'source',
  ],
}
