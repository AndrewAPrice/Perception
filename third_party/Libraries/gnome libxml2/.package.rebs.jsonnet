{
    package_type: "library",
    public_include_directories: [
        "public",
    ],
    sources: [
        "source",
    ],
    dependencies +: [
        "madler zlib",
        "unicode-org icu"
    ]
}