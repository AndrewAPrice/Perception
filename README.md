# Perception

Perception is a hobby operating system. It is a [x86-64](https://en.wikipedia.org/wiki/X86-64) operating system built around a [microkernel](https://en.wikipedia.org/wiki/Microkernel).

## Features
Very few right now. We can load ELF modules and make system calls.

## Building and running
See [BUILDING.md]. Perception has only been tested in [QEMU](https://www.qemu.org/).

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