{
  should_skip: 1,
  skip_for_tests: true,
  package_type: 'library',
  dependencies+: [
    'musl',
    'libcxx',
    'libsdl-org SDL',
  ],
  public_include_directories: [
    'public',
  ],
  include_directories: [
    'source',
    'source/common',
    'source/core',
    'source/al',
    'source/alc',
    'include',
  ],
  defines+: [
    'ALSOFT_API=',
    'AL_BUILD_LIBRARY=',
    'AL_ALEXT=',
    'AL_ALEXT_PROTOTYPES=1',
    'FMT_HEADER_ONLY=1',
  ],
  source_directories: [
    'source',
  ],
  files_to_ignore: [
    'source/core/mixer/mixer_neon.cpp',
    'source/core/rtkit.cpp',
  ],
}
