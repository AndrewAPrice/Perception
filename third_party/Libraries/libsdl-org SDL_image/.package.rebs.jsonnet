{
  skip_for_tests: true,
  package_type: 'library',
  defines+: [
    'SDL_DYNAMIC_API=0',
    'LOAD_PNG=1',
    'LOAD_BMP=1',
    'LOAD_JPG=1',
    'LOAD_GIF=1',
    'LOAD_TGA=1',
    'LOAD_WEBP=1',
    'LOAD_SVG=1',
    'LOAD_QOI=1',
    'LOAD_PCX=1',
    'LOAD_PNM=1',
    'LOAD_XPM=1',
    'LOAD_XCF=1',
    'LOAD_LBM=1',
    'LOAD_XV=1',
  ],
  dependencies+: [
    'musl',
    'libsdl-org SDL',
    'libpng',
    'libjpeg-turbo',
    'libwebp',
  ],
  public_include_directories: [
    'public',
  ],
  source_directories: [
    'source',
  ],
}
