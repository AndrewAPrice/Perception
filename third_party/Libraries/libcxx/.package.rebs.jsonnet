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
    'source/cxa_noexception.cpp',
    'source/vendor/apple/shims.cpp'
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
    '_LIBCPP_HAS_THREADS',
    '_LIBCPP_HAS_MONOTONIC_CLOCK',
    '_LIBCPP_PSTL_BACKEND_STD_THREAD',
    'LIBCXXRT',
    '_LIBCPP_HAS_RANDOM_DEVICE',
    '_LIBCPP_HAS_LOCALIZATION',
    '_LIBCPP_HAS_TIME_ZONE_DATABASE',
    '_LIBCPP_HAS_FILESYSTEM',
    '_LIBCPP_HAS_THREAD_API_PTHREAD',
    '_LIBCPP_HARDENING_MODE=2',
    '_LIBCPP_ASSERTION_SEMANTIC_DEFAULT=4',
    '_LIBCPP_HAS_MUSL_LIBC',
    'LIBC_NAMESPACE=__llvm_libc'
  ],
}
