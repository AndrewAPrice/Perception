{
  skip_for_tests: true,
  package_type: 'library',
  defines+: [
    'SDL_DYNAMIC_API=0',
    'MUSIC_OGG=1',
    'OGG_USE_STB=1',
    'MUSIC_MP3=1',
    'MUSIC_MP3_MINIMP3=1',
    'MUSIC_FLAC=1',
    'MUSIC_FLAC_DRFLAC=1',
    'MUSIC_WAV=1',
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
    'source/codecs',
    'public',
  ],
  source_directories: [
    'source',
  ],
}
