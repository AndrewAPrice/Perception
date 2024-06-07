{
    package_type: "library",
    public_include_directories: [
        "public",
    ],
    defines: [
        "qLinux"
    ],
    sources: [
        "source",
    ],
    dependencies +: [
        "madler zlib",
        "libjpeg-turbo"
    ]
}