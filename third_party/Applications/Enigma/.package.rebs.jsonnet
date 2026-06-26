{
  skip_for_tests: true,
  package_type: 'application',
  defines+: [
    'HAS_SOCKLEN_T=1',
  ],
  dependencies+: [
    'musl',
    'libcxx',
    'madler zlib',
    'freetype',
    'curl',
    'apache xerces-c',
    'libsdl-org SDL',
    'libsdl-org SDL_image',
    'libsdl-org SDL_ttf',
    'libsdl-org SDL_mixer',
  ],
  include_directories: [
    'source',
    'source/src',
    'source/lib-src',
    'source/lib-src/enigma-core',
    'source/lib-src/enet/include',
    'source/lib-src/lua',
    'source/lib-src/oxydlib',
    'source/lib-src/tinygettext/include',
  ],
  source_directories: [
    'source'
  ],
  asset_directories: [
    'assets',
  ],
}
