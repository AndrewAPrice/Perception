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
    sources: [
        "source",
    ],
    dependencies +: [
        "madler zlib"
    ]
}