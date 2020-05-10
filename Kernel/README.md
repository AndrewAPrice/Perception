The kernel for [Perception](../README.md). It is written in C and intended to be booted with a multiboot bootloader that supports modules, such as GRUB 2.

Being a [microkernel](https://en.wikipedia.org/wiki/Microkernel), it is intended to stay very small and generic.

The features of the kernel are:

- It runs in x86-64 long mode.
- Virtual memory management.
- Process and thread management and scheduling.
- Dispatching interrupts and gatekeeping IO.
- Events and RPCs between processes.
