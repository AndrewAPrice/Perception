{
  package_type: 'library',
  asset_directories: [
    'assets',
  ],
  public_include_directories: [
    'public',
  ],
  defines+: [
    'U_I18N_IMPLEMENTATION',
    'U_COMMON_IMPLEMENTATION',
    'ICU_DATA_DIR="\\"/Libraries/unicode-org icu/assets/\\""',
  ],
  source_directories: [
    'source',
  ],
  dependencies+: [
    'harfbuzz icu-le-hb',
  ],
}
