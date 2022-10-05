# Building

## Dependencies
- The build system depends on Node.js and Git.
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
- Git: https://git-scm.com/downloads

You can find precompiled x86_64-elf binaries online with a bit of hunting. Examples:
- https://github.com/lordmilko/i686-elf-tools
- https://blog.futuremillennium.com/post/140862688548/getting-started-with-os-development-on-windows

Otherwise, you can [follow these instructions](https://wiki.osdev.org/GCC_Cross-Compiler) to build your own GCC cross compiler.

Likewise with GRUB 2.
- https://www.aioboot.com/en/install-grub2-from-windows/

Otherwise, you can will have to [build GRUB 2 from source] (https://www.gnu.org/software/grub/grub-download.html).

### Mac OS
If you have [Homebrew](https://brew.sh/), you can install everything you need with:

```
brew tap nativeos/i386-elf-toolchain
brew install node git nasm qemu x86_64-elf-binutils x86_64-elf-gcc i386-elf-grub xorriso`
```

If you're building on an M1 and get:
```
Undefined symbols for architecture arm64:
  "_host_hooks", referenced from:
      gt_pch_save(__sFILE*) in libbackend.a(ggc-common.o)
      gt_pch_restore(__sFILE*) in libbackend.a(ggc-common.o)
      toplev::main(int, char**) in libbackend.a(toplev.o)
```

Run:
`brew edit i386-elf-gcc`
Change the `url`, `version`, `sha256` to:

```
	url "https://ftp.gnu.org/gnu/gcc/gcc-12.1.0/gcc-12.1.0.tar.xz"
  version "12.1.0"
  sha256 "62fd634889f31c02b64af2c468f064b47ad1ca78411c45abe6ac4b5f8dd19c7b"
```

Then rerun the above `brew install` command to continue installing everything.

## Preparation
- Create a file Build/tools.json with the paths to the above tools.
```json
{
	"ar": "x86_64-elf-gcc-ar",
	"gas": "x86_64-elf-gcc-as",
	"nasm": "nasm",
	"gcc": "x86_64-elf-gcc",
	"grub-mkrescue": "grub-mkrescue",
	"ld": "x86_64-elf-ld",
	"qemu": "qemu-system-x86_64"
}
```

## Building
Call all commands from inside the Build directory. Here are some useful ones:

- `./build all` - Builds everything. The kernel, all applications, and creates a Perception.iso in the root directory.
- `./build run` - Builds everything and starts QEMU.
- `./build run <application> --local` - Builds an application and runs it locally in the host OS.
- `./build clean` - Cleans up built files.
- `./build all --prepare` - Prepares applications and libraries (downloads third party files, generates auto-complete metadata, transpiles Permebuf files, etc.) without actually building anything.

You can lean more about the build system and more commands in [Build/README.md](Build/README.md).

## Code Completion Support
Upon building, the build system generates `.clang_complete` files can be used for code completion. I use [Sublime Text](https://www.sublimetext.com/) with [EasyClangComplete](https://github.com/niosus/EasyClangComplete) which works out of the box. It's also helpful to tell your code completer that we're using C17 and C++20, and not to use default includes. If you're using Sublime with EasyClangComplete, you can open the project `build/perception.sublime-project`.
