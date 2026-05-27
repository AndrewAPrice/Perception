# Perception

Perception is a hobby operating system. It is a [x86-64](https://en.wikipedia.org/wiki/X86-64) operating system built around a [microkernel](https://en.wikipedia.org/wiki/Microkernel).

[The kernel](Services/Kernel/README.md) is written in C++. I use [a custom build system](Build/README.md). I provide a C++ runtime for libraries, services, drivers, and programs.

## Features
Very few right now.

* We have a C and C++ standard library for user applications.
* Processes can discover and register services and send RPCs to one another.
* Mounting ISO 9660 disks and reading files.
* We have a GUI, window manager, and a UI toolkit.
* Launching userland applications.
* Press ESCAPE to open the launcher.
* There's a permissions system.
* Some simple applications like File Manager, calculator, and image viewer.

## Building and running
See [building.md](building.md). Perception has only been tested in [QEMU](https://www.qemu.org/). Has a graphical interface and also outputs debugging text via COM1.

## Directory Structure
- Applications - Applications/user programs.
- Dump - Code from an old version that will be useful at some point in the future.
- Drivers - Drivers.
- fs - The root file system that is turned into a disk image.
- Libraries - The libraries for building user programs.
- Services - Services and the kernel.
- third_party - 3rd party code I didn't write. They have different licensing.

## Repository scope
The scope of this repository is strictly for the core of the operating system. Applications and libraries beyond what would be considered the core of the operating system should live in their own repositories.

## Contributing
Being a personal hobby project, I'm not currently accepting other contributors. Feel free to build applications on top of my OS and let me know about them!

This is not an officially supported Google project.
