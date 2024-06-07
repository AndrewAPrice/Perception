{
    package_type: "library",
    public_include_directories: [
        "public",
    ],
    defines: [
        "qLinux"
    ],
    source_directories: [
        "source",
    ],
    dependencies +: [
        "madler zlib",
        "libjpeg-turbo"
    ]
}