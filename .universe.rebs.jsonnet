{
  local archiver = if is_testing then 'ar' else 'llvm-ar',
  local cpp_compiler = if is_testing then 'clang++' else 'clang',
  local linker = if is_testing then 'clang++' else 'ld.lld',
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
    local c_compiler = if is_testing then 'clang' else 'clang',
    local c_command_prefix = cpp_compiler + c_optimizations +
                             ' -c' +
                             (if is_testing then '' else ' --target=x86_64-unknown-none-elf -nostdinc -mno-red-zone') +
                             ' -fdata-sections -ffunction-sections -fPIC' +
                             (if is_testing then '' else ' -isystem "${clangresources}/include"'),
    local c_command_prefix_for_c = c_compiler + c_optimizations +
                             ' -c' +
                             (if is_testing then '' else ' --target=x86_64-unknown-none-elf -nostdinc -mno-red-zone') +
                             ' -fdata-sections -ffunction-sections -fPIC' +
                             (if is_testing then '' else ' -isystem "${clangresources}/include"'),
    local cpp_command = c_command_prefix + ' -std=c++23 ' + (if is_testing then '' else '-nostdinc++ ') + '${cdefines} ${cincludes} -MD -MF ${deps file} -o ${out} ${in}',
    cpp: cpp_command,
    cc: cpp_command,
    c: c_command_prefix_for_c +
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
      linker + application_linker_optimizations + (if is_testing then '' else ' -nostdlib -z max-page-size=4096') + ' -o ${out} ${in} -L ${shared_library_path} ${shared_libraries}'
    else if package_type == 'library' then
      if is_testing then
        linker + ' -shared -o ${out} ${in}'
      else
        linker + ' -strip-all -shared -o ${out} ${in}'
    else
      '',
  static_linker_command:
    if is_testing then
      archiver + ' rcs ${out} ${in}'
    else if package_type == 'application' then
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
    'third_party/Applications',
    'third_party/Libraries',
    'Services',
  ],
  defines: [
    'PERCEPTION',
  ] + (if is_testing then ['TEST'] else []) + if optimization_level == 'optimized' then
    ['optimized_BUILD_']
  else if optimization_level == 'fast' then
    ['fast_BUILD_']
  else if optimization_level == 'debug' then
    ['debug_BUILD_']
  else
    [],
  dependencies: [
    'libcxx',
    'LLVM Compiler-RT',
    'musl',
    'Perception Core',
  ],
  test_dependencies: [
    'Perception Test'
  ],
  local iso_path = '${temp directory}/image.iso',
  local fs_path = '${temp directory}/fs/',
  global_run_command:
    'grub-mkrescue -o "' + iso_path + '" "' + fs_path + '"&&' +
    'qemu-system-x86_64 -boot d -cdrom "' + iso_path + '" -m 2048 -serial stdio -device isa-debug-exit,iobase=0xf4,iosize=0x04 -netdev user,id=net0 -device virtio-net-pci,netdev=net0',

}
