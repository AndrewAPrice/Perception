# Building

## Dependencies
- The build system depends on Node.js.
- The compilation depends on NASM, and GCC, binutils, and LD that are able to build x86_64 ELF binaries.
  - (It would be nice if we could also support Clang, but the generated machine code (for both the kernel and userland) code crashes.)
- Building the bootable ISO depends on GRUB 2.
- Although you can use any emulator, the build system invokes [QEMU](https://www.qemu.org/).

### Linux
Check if your distro's package manager has a prebuilt version of GCC/binutils/LD that supports x86_64 ELF. x86_64-elf-gcc. If not, [follow these instructions](https://wiki.osdev.org/GCC_Cross-Compiler) to build your own GCC cross compiler.

### Windows
- Node.js https://nodejs.org/en/download/
- NASM: https://www.nasm.us/
- QEMU: https://www.qemu.org/download/

You can find precompiled x86_64-elf binaries online with a bit of hunting. Examples:
- https://github.com/lordmilko/i686-elf-tools
- https://blog.futuremillennium.com/post/140862688548/getting-started-with-os-development-on-windows

Otherwise, you can [follow these instructions](https://wiki.osdev.org/GCC_Cross-Compiler) to build your own GCC cross compiler.

Likewise with GRUB 2.
- https://www.aioboot.com/en/install-grub2-from-windows/

Otherwise, you can will have to [build GRUB 2 from source] (https://www.gnu.org/software/grub/grub-download.html).

### Mac OS
If you have [Homebrew](https://brew.sh/), you can install everything you need with:

`brew install node nasm qemu x86_64-elf-binutils x86_64-elf-gcc x86_64-elf-ld i386-elf-grub`

## Preparation
- Create a file Build/tools.json with the paths to the above tools.
```json
{
	"nasm": "nasm",
	"gcc": "x86_64-elf-gcc",
	"grub-mkrescue": "grub-mkrescue",
	"ld": "x86_64-elf-ld",
	"qemu": "qemu-system-x86_64"
}
```

- Install
```sh
cd Build && npm install
```

## Building
Call all commands from inside the Build directory.

- `node build` - Builds everything. The kernel, all applications, and creates a Perception.iso in the root directory.
- `node build run` - Builds everything and starts QEMU.
- `node build kernel` - Builds the kernel.
- `node build application <application>` - Builds a particular application.
- `node build library <library>` - Builds a particular library.
- `node build clean` - Cleans up built files.