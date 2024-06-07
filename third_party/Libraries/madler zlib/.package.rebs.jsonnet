{
    package_type: "library",
    public_include_directories: [
        "public",
    ],
    defines: [
		"_LARGEFILE64_SOURCE",
		"Z_HAVE_UNISTD_H"
    ],
    source_directories: [
        "source",
    ],
}