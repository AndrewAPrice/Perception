# Perception

Perception is a hobby operating system. It is a [x86-64](https://en.wikipedia.org/wiki/X86-64) operating system built around a [microkernel](https://en.wikipedia.org/wiki/Microkernel).

[The kernel](Kernel/README.md) is written in C. I use [a custom build system](Build/README.md). I provide a C++ runtime for libraries, services, drivers, and programs. I created an [interface descriptor language](https://en.wikipedia.org/wiki/Interface_description_language) known as [Permebuf](Build/Permebuf.md).

## Features
Very few right now.

* We have a C and C++ standard library for user applications.
* Programs can discover and register [Permebuf](Build/Permebuf.md) services and send RPCs to one another.
* We have a tiling window manager (floating windows are also supported!)
* Mounting ISO 9660 disks and reading files.
* Press ESCAPE to open the launcher.

## Building and running
See [building.md](building.md). Perception has only been tested in [QEMU](https://www.qemu.org/). Currently text only and outputs via COM1.

## Directory Structure
- Applications - User programs.
- Build - The build system.
- Dump - Code from an old version that will be useful at some point in the future.
- fs - The root file system that is turned into a disk image.
- Kernel - The kernel sources.
- Libraries - The libraries for building user programs.
- third_party - 3rd party code I didn't write. They have different licensing.

## Repository scope
The scope of this repository is strictly for the core of the operating system. Applications and libraries beyond what would be considered the core of the operating system should live in their own repositories.

## Contributing
Being a personal hobby project, I'm not currently accepting other contributors. Feel free to build applications on top of my OS and let me know about them!

This is not an officially supported Google project.
