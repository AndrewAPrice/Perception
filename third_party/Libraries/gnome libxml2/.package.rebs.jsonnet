{
    package_type: "library",
    public_include_directories: [
        "public",
    ],
    source_directories: [
        "source",
    ],
    dependencies +: [
        "madler zlib",
        "unicode-org icu"
    ]
}