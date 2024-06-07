{
    package_type: "library",
    public_include_directories: [
        "public",
    ],
    defines: [
		"HAVE_FREETYPE",
		"HAVE_ICU"
    ],
    sources: [
        "source",
    ],
    dependencies +: [
		"freetype",
		"unicode-org icu"
    ]
}