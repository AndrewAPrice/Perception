{
  skip_for_tests: true,
  package_type: 'library',
  public_include_directories: [
    'public',
  ],
  defines+: [
    'HAVE_CONFIG_H',
    'ENABLE_LIBXML2',
    'ENABLE_FREETYPE',
    'CONFIGDIR="\\"/Libraries/fontconfig/assets/config\\""',
    'FONTCONFIG_PATH="\\"/Libraries/fontconfig/assets\\""',
    'FC_CACHEDIR="\\"/Temporary/Libraries/fontconfig/cache\\""',
    'FC_TEMPLATEDIR="\\"/Libraries/fontconfig/assets/templates\\""',
    '__linux__',
    'HAVE_VASPRINTF',
  ],
  include_directories: [
    'source',
  ],
  source_directories: [
    'source',
  ],
  files_to_ignore: [
    'source/src/fcgenericalias.c',
    'source/src/fcobjshash.gperf.c',
  ],
  dependencies+: [
    'freetype',
    'gnome libxml2',
  ],
  asset_directories: [
    'assets',
  ],
}
