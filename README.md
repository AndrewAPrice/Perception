# Perception

Perception is a hobby operating system. There are multiple components to this project. Shovel is a programming language being developed for Perception.

Shovel and Turkey are being developed seperately from Perception, so that they may be embedded into any system or application.


## Running
Either build the operating system from source or download a prebuilt image (coming soon) and boot it. Use the Windows key to access the shell that
allows you to launch applications.

To set a custom resolution, press 'E' in GRUB, and replace the 'auto' on "set gfxpayload=auto" with a resolution (in the format of WidthxHeightxBPP or WidthxHeight) and press F10 to boot.

## Features:
Some of the features of the operating system are:
- A shell that discovers programs on mounted disks and manages running processes (press the Windows key to access it at any time).
- Asynchronous/event-based programming API.
- Unix style VFS with ISO 9660 read support.
- Compositing tabbing window manager.
- VBE video and a fall back VGA driver (press F12 to enable full screen dithering in low color depth modes).


## Directory Structure
- Applications - The sources for the applications that come with Perception.
- build - Tools for cross-compiling the applications and libraries.
- fs - The root file system that is turned into a disk image.
- kernel - The kernel sources.
- Libraries - The sources of libraries that run under Perception.
- shovel - Tools related to the Shovel language build system - compiler, assembler
- turkey - Turkey is a virtual machine that runs Shovel programs.


## Todo
What I want to accomplish before public build 1:
- Prettier GUI.
- Multiple keyboard layouts.
- RAM disk with reading & writing.
- Loading programs.
- A GUI widget library.
- A file manager.
- A terminal emulator.
- Some simple games (Minesweeper, Freecell).
- A tool chain that runs under the OS.
- An IDE for native and Shovel applications

What I want to accomplish with Shovel include:
- Static typing.
- Full JIT.
- Merge the compiler and runtime, so Shovel can be dynamically evaluated.

## Want to help?
I would be interested in accepting patches, mainly those that improve performance (such as new optimizations for Turkey) or clean up
code (the SSA generator is really messy!)

Contains DejaVu fonts: http://dejavu-fonts.org
