{
    package_type: "library",
    assets: [
        "assets"
    ],
    public_include_directories: [
        "public",
    ],
    defines: [
		"U_I18N_IMPLEMENTATION",
		"U_COMMON_IMPLEMENTATION",
		"ICU_DATA_DIR=\"\\\"/Libraries/unicode-org icu/assets/\\\"\""
    ],
    sources: [
        "source"
    ],
    dependencies+: [
		"harfbuzz icu-le-hb"
    ],
}