{
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
            "  -mcmodel=kernel -c --target=x86_64-unknown-none-elf -fdata-sections -ffunction-sections -nostdinc -ffreestanding -fno-builtin -mno-sse -fno-unwind-tables -fno-exceptions ",
        "cc": cpp_command + " -std=c++20 ${cdefines} ${cincludes} -MD -MF ${deps_out} -o ${out} ${in}",
        "c": cpp_command +
            " -c -std=c17 ${cdefines} ${cincludes} -MD -MF ${deps_out} -o ${out} ${in}",
        // Intel ASM:
        "asm": "nasm -felf64 -dPERCEPTION -o ${out} ${in}",
    },
    local linker_optimizations =
        if optimization_level == "optimized" then " -s "
        else " -g ",
    "linker_command": 
      linker + linker_optimizations + " -T Applications/Kernel/source/linker.ld -o ${out} ${in}",
    "output_extension": "app",
    "include_directories": [
        "source"
    ]
}