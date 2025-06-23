{
  package_type: 'library',
  public_include_directories: [
    'public',
  ],
  defines+: [
    'SK_CODEC_DECODES_JPEG',
    'SK_CODEC_DECODES_JPEG_GAINMAPS',
    'SK_DISABLE_LEGACY_FONTMGR_FACTORY',
    'SK_DISABLE_LEGACY_FONTMGR_REFDEFAULT',
    'SK_GANESH',
    'SK_GANESH_ENABLED',
    'SK_SHAPER_HARFBUZZ_AVAILABLE',
    'SK_SHAPER_UNICODE_AVAILABLE',
    'SK_UNICODE_AVAILABLE',
    'SK_UNICODE_ICU_IMPLEMENTATION',
    'SKCMS_DISABLE_HSW',
    'SKCMS_DISABLE_SKX',
    'SK_BUILD_FOR_UNIX',
  ],
  include_directories: [
    'source',
  ],
  source_directories: [
    'source',
  ],
  dependencies+: [
    'fontconfig',
    'harfbuzz',
    'libexpat',
    'libjpeg-turbo',
    'Perception UI',
    'richgel999 fpng',
    'unicode-org icu',
  ],
}
