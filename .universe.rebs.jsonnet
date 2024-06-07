{
    local archiver = "llvm-ar",
    local cpp_compiler = "clang",
    local linker = "ld.lld",
    "build_commands": {
        // C and C++:
        local c_optimizations =
            if optimization_level == "optimized" then
            " -g -O3 -fomit-frame-pointer -flto"
            else if optimization_level == "debug" then
            " -g -Og"
            else
            "",
        local cpp_command = cpp_compiler + c_optimizations +
            " -c --target=x86_64-unknown-none-elf -fdata-sections -ffunction-sections -nostdinc -ffreestanding -fno-builtin -mno-sse -fno-unwind-tables -fno-exceptions ",
        "cc": cpp_command + " -std=c++23 ${cdefines} ${cincludes} -MD -MF ${deps_out} -o ${out} ${in}",
        "c": cpp_command +
            " -c -std=c17 ${cdefines} ${cincludes} -MD -MF ${deps_out} -o ${out} ${in}",
        // Intel ASM:
        "asm": "nasm -felf64 -dPERCEPTION -o ${out} ${in}",
    },
    local application_linker_optimizations =
        if optimization_level == "optimized" then " -O3 -g -s --gc-sections "
        else " -g ",
    "linker_command":
      if self.package_type == "application" then
        linker + application_linker_optimizations + " -nostdlib -z max-page-size=1 -o ${out} ${in}"
      else if self.package_type == "library" then
        archiver + " rcs ${out} ${in}"
      else
        "",
    "output_extension":
        if self.package_type == "application" then
        "app"
        else if self.package_type == "library" then
        "lib"
        else
        "",
    "default_os": "perception",
    "default_arch": "x86-64",
    "package_directories": [
        "Applications",
        "Libraries",
        "third_party/Libraries"
    ],
    "dependencies": [
      "musl",
      "libcxx"
    ]
}