# Building

## Dependencies
- [REBS (Really Easy Build System)](https://github.com/AndrewAPrice/rebs) (`/usr/local/bin/rebs` or on your `PATH`).
- LLVM/Clang (`clang`, `clang++`, `llvm-ar`, and `ld.lld`) capable of building `x86_64-unknown-none-elf` binaries (C17 and C++23).
- [NASM](https://www.nasm.us/) for assembling `.asm` files.
- GRUB 2 (`grub-mkrescue`) and `xorriso` for building the bootable ISO image.
- [QEMU](https://www.qemu.org/) (`qemu-system-x86_64`) and Python 3 for running Perception in an emulator and viewing demultiplexed serial logs.

### macOS

If you have [Homebrew](https://brew.sh/), you can install the required toolchain and emulator dependencies with:

```bash
brew tap nativeos/i386-elf-toolchain
brew install git nasm qemu llvm i386-elf-grub xorriso lld
```

Ensure Homebrew's `llvm` and `lld` binaries are on your `PATH`, and install [REBS](https://github.com/AndrewAPrice/rebs) to `/usr/local/bin/rebs`.

### Linux
Install `clang`, `lld`, `llvm`, `nasm`, `grub-pc-bin` / `grub-efi-amd64-bin` (`grub-mkrescue`), `xorriso`, `qemu-system-x86_64`, and `python3` via your distribution's package manager, along with [REBS](https://github.com/AndrewAPrice/rebs).

## Building and Running

Build configurations are defined in [`.universe.rebs.jsonnet`](.universe.rebs.jsonnet) and per-package `.package.rebs.jsonnet` files. Run REBS from the repository root:

- `/usr/local/bin/rebs --all` - Builds the kernel, drivers, services, libraries, and applications, creates `.build/<configuration>/image.iso`, and launches Perception in QEMU with the interactive serial log viewer ([`tools/run_qemu.sh`](tools/run_qemu.sh)).
- `/usr/local/bin/rebs --all --build` - Builds everything and packages the bootable ISO without launching QEMU.
- `/usr/local/bin/rebs --test "<Package Name>"` - Builds and runs host-native unit tests for a specific package.

### Optimization Levels
You can pass an optimization flag to any `rebs` invocation:
- `--fast` *(default)* - Builds with `-O2` and frame pointers enabled.
- `--debug` - Builds with `-Og` and debug symbols for debugging.
- `--optimized` - Builds with `-Os`, LTO (`-flto`), and stripped debug symbols.

You can learn more about REBS and its configuration options in the [REBS repository](https://github.com/AndrewAPrice/rebs).

## Running on Physical Hardware
See [docs/network_booting.md](docs/network_booting.md) for instructions on booting Perception on a physical x86-64 PC via UEFI Network Boot (PXE/TFTP + HTTP) over Ethernet.

## Code Completion & Debugging
- **Code Completion (`clangd`)**: Run `/usr/local/bin/rebs --all --generate-clangd` to generate `.clangd` configuration files across packages for accurate C17 and C++23 code completion in VS Code and other `clangd`-compatible editors.
- **Debugging**: See [Debugging.md](Debugging.md) for instructions on using GDB, LLDB, and the interactive `COM1` serial log viewer ([`tools/log_viewer.py`](tools/log_viewer.py)).
