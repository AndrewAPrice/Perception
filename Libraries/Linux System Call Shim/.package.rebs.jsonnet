{
    package_type: "library",
    include_directories: [
        "source"
    ],
    public_include_directories: [
        "public"
    ],
    sources: [
        "source"
    ],
    dependencies +: [
        "perception"
    ]
}