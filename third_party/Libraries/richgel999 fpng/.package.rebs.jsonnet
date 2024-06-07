{
    package_type: "library",
    public_include_directories: [
        "public",
    ],
    defines: [
		"FPNG_NO_SSE"
    ],
    source_directories: [
        "source"
    ],
    dependencies+: [
		"libclang compiler headers"
    ],
}