{
  package_type: 'library',
  public_include_directories: [
    'public',
  ],
  include_directories: [
    'source',
    'source/simd/x86_64',
  ],
  files_to_ignore: [
    'source/jccolext.c',
    'source/jcphuff.c',
    'source/jdcol565.c',
    'source/jdcolext.c',
    'source/jdmrg565.c',
    'source/jdmrgext.c',
    'source/jstdhuff.c',
    'source/turbojpeg-mp.c',
    'source/simd/x86_64/jccolext-avx2.asm',
    'source/simd/x86_64/jccolext-sse2.asm',
    'source/simd/x86_64/jcgryext-avx2.asm',
    'source/simd/x86_64/jcgryext-sse2.asm',
    'source/simd/x86_64/jdcolext-avx2.asm',
    'source/simd/x86_64/jdcolext-sse2.asm',
    'source/simd/x86_64/jdmrgext-avx2.asm',
    'source/simd/x86_64/jdmrgext-sse2.asm',
  ],
  source_directories: [
    'source',
  ],
}
