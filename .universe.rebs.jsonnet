{
  local archiver = 'llvm-ar',
  local cpp_compiler = 'clang',
  local linker = 'ld.lld',
  local package_type = self.package_type,
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
                             ' -c --target=x86_64-unknown-none-elf -fdata-sections -ffunction-sections -nostdinc -mno-red-zone -fPIC',
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
    if optimization_level == 'optimized' then ' -O3 -g -s'
    else ' -g ',
  linker_command:
    if package_type == 'application' then
      linker + application_linker_optimizations + ' -nostdlib -z max-page-size=4096 -o ${out} ${in} -L ${shared_library_path} ${shared_libraries}'
    else if package_type == 'library' then
      linker + ' -strip-all -shared -o ${out} ${in}'
    else
      '',
  static_linker_command:
    if package_type == 'application' then
      linker + application_linker_optimizations + ' -nostdlib -z max-page-size=4096 -o ${out} ${in}'
    else if package_type == 'library' then
      archiver + ' rcs ${out} ${in}'
    else
      '',
  output_prefix:
    if package_type == 'library' then
     'lib'
    else
     '',
  output_extension:
    if package_type == 'application' then
      'app'
    else if package_type == 'library' then
      'so'
    else
      '',
  destination_directory:
    if package_type == 'application' then
      '${temp directory}/fs/Applications/${package name}'
    else if package_type == 'library' then
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
    'Perception Core',
  ],
  local iso_path = '${temp directory}/image.iso',
  local fs_path = '${temp directory}/fs/',
  global_run_command:
    'grub-mkrescue -o "' + iso_path + '" "' + fs_path + '"&&' +
    'qemu-system-x86_64 -boot d -cdrom "' + iso_path + '" -m 512 -serial stdio',

}
