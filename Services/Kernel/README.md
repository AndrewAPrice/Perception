The kernel for [Perception](../README.md). It is written in C++ and intended to be booted with a multiboot bootloader that supports modules, such as GRUB 2.

Being a [microkernel](https://en.wikipedia.org/wiki/Microkernel), it is intended to stay very small and generic.

The features of the kernel are:

- It runs in x86-64 long mode.
- Process management and virtual memory isolation.
- Thread management and scheduling.
- Dispatching interrupts and gatekeeping IO.
- Events and RPCs between processes.
- Loads ELF multiboot modules (after initializion, the kernel expects all other programs to be loaded by a userland loader.)

As a pure microkernel, the kernel has no kernel-level threads, background daemons, or asynchronous worker runtimes. The kernel only executes when a physical CPU core traps into Ring 0 via:

- A hardware interrupt (Local APIC timer, IPI, or device IRQ),
- A CPU exception (Page Fault, General Protection Fault), or
- A system call (syscall instruction).

The kernel could be used independently from the rest of the operating system (for example, bundling the kernel with a completely different set of services and programs to make a different operating system, or if you wanted to deploy some code near-bear metal with threading support.)
