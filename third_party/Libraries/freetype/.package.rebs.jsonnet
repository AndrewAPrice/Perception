{
    package_type: "library",
    public_include_directories: [
        "public",
    ],
    defines: [
        "_LIB",
        "FT2_BUILD_LIBRARY",
        "FT_CONFIG_OPTION_SYSTEM_ZLIB",
        "HAVE_UNISTD_H",
        "HAVE_FCNTL_H",
        "NDEBUG"
    ],
    files_to_ignore: [
        "source/gzip/zutil.c",
        "source/gzip/inffast.c",
        "source/gzip/inflate.c",
        "source/gzip/inftrees.c",
        "source/gzip/adler32.c",
        "source/gzip/crc32.c",
        "source/lzw/ftzopen.c"
    ],
    source_directories: [
        "source",
    ],
    dependencies +: [
        "madler zlib"
    ]
}