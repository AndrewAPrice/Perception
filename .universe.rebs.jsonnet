{
  local archiver = 'llvm-ar',
  local cpp_compiler = 'clang',
  local linker = 'ld.lld',
  build_commands: {
    // C and C++:
    local c_optimizations =
      if optimization_level == 'optimized' then
        ' -g -O3 -fomit-frame-pointer -flto'
      else if optimization_level == 'debug' then
        ' -g -Og'
      else
        ' -g -O2 ',
    local c_command_prefix = cpp_compiler + c_optimizations +
                             ' -c --target=x86_64-unknown-none-elf -fdata-sections -ffunction-sections -nostdinc -ffreestanding -fno-builtin -mno-red-zone',
    local cpp_command = c_command_prefix + ' -std=c++23 -nostdinc++ ${cdefines} ${cincludes} -MD -MF ${deps file} -o ${out} ${in}',
    cpp: cpp_command,
    cc: cpp_command,
    c: c_command_prefix +
       ' -std=c17 ${cdefines} ${cincludes} -MD -MF ${deps file} -o ${out} ${in}',
    // AT&T asm:
    local att_asm = c_command_prefix + ' ${cdefines} ${cincludes} -c -o ${out} ${in}',
    s: att_asm,
    S: att_asm,
    // Intel ASM:
    asm: 'nasm -felf64 ${cdefines} ${cincludes} -o ${out} ${in}',
  },
  local application_linker_optimizations =
    if optimization_level == 'optimized' then ' -O3 -g -s --gc-sections '
    else ' -g ',
  linker_command:
    if self.package_type == 'application' then
      linker + application_linker_optimizations + ' -nostdlib -z max-page-size=1 -o ${out} ${in}'
    else if self.package_type == 'library' then
      archiver + ' rcs ${out} ${in}'
    else
      '',
  output_extension:
    if self.package_type == 'application' then
      'app'
    else if self.package_type == 'library' then
      'lib'
    else
      '',
  destination_directory:
    if self.package_type == 'application' then
      '${temp directory}/fs/Applications/${package name}'
    else if self.package_type == 'library' then
      '${temp directory}/fs/Libraries/${package name}'
    else
      '',
  default_os: 'perception',
  default_arch: 'x86-64',
  package_directories: [
    'Applications',
    'Drivers',
    'Libraries',
    'third_party/Libraries',
    'Services',
  ],
  defines: [
    'PERCEPTION',
  ] + if optimization_level == 'optimized' then
    ['optimized_BUILD_']
  else if optimization_level == 'fast' then
    ['fast_BUILD_']
  else if optimization_level == 'debug' then
    ['debug_BUILD_']
  else
    [],
  dependencies: [
    'libclang compiler headers',
    'libcxx',
    'LLVM Compiler-RT',
    'musl',
  ],
  local iso_path = '${temp directory}/image.iso',
  local fs_path = '${temp directory}/fs/',
  global_run_command:
    'grub-mkrescue -o "' + iso_path + '" "' + fs_path + '"&&' +
    'qemu-system-x86_64 -boot d -cdrom "' + iso_path + '" -m 512 -serial stdio',

}
