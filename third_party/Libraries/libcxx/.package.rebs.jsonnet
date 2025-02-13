{
  package_type: 'library',
  dependencies+: [
    'perception',
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
  files_to_ignore: [
    'source/cxa_noexception.cpp'
  ],
  include_priority: 1,
  defines+: [
    '_LIBCPP_BUILDING_LIBRARY',
    'LIBCXX_BUILDING_LIBCXXABI',
    '_LIBUNWIND_DISABLE_VISIBILITY_ANNOTATIONS',
    '_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE',
  ],
  public_defines: [
    '_LIBCPP_HAS_THREAD_API_C11',
    'LIBCXXRT',
    '_LIBCPP_HAS_THREAD_API_PTHREAD',
    '_LIBCPP_HARDENING_MODE=2',
    '_LIBCPP_PSTL_CPU_BACKEND_THREAD',
    '_LIBCPP_HAS_MUSL_LIBC',
  ],
}
