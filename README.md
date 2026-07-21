# Perception

Perception is a hobby operating system. It is a [x86-64](https://en.wikipedia.org/wiki/X86-64) operating system built around a [microkernel](https://en.wikipedia.org/wiki/Microkernel).

[The kernel](Services/Kernel/README.md) is written in C++. I use [a custom build system](Build/README.md). I provide a C++ runtime for libraries, services, drivers, and programs.

## Features

* A x86-64 native microkernel.
* Full C/C++ standard library support.
* Processes can discover and register services and send RPCs to one another.
* Mounting ISO 9660 disks and reading files.
* Basic hardware support (PS/2 keyboard, IDE storage, Multiboot framebuffer and Virtio graphics, Virtio network).
* A window manager, custom C++ UI framework, and an interactive UI debugger.
* Launching userland applications.
* There's a permissions system (e.g. "Allow File Manger to launch applications?") and a registry with an editor.
* Some simple applications like File Manager, Calculator, and Image Viewer.
* Some simple games (2048, Minesweeper, Snake).

## Building and running
See [building.md](building.md). Perception has only been tested in [QEMU](https://www.qemu.org/). It outputs debugging text via COM1.

## Directory Structure
- Applications - Applications/user programs.
- Drivers - Drivers.
- Libraries - The libraries for building user programs.
- Services - Services and the kernel.
- third_party - 3rd party code I didn't write. They have different licensing.

## Contributing
Being a personal hobby project, I'm not currently accepting other contributors. Feel free to build applications on top of my OS and let me know about them!

This is not an officially supported Google project.
