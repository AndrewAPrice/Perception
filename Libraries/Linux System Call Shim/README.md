A shim converting Linux system calls to using Perception system calls and services.

[musl libc](https://musl.libc.org/) is built around the Linux system calls. Rather than modify or create a custom [C standard library](https://en.wikipedia.org/wiki/C_standard_library), musl is used because it is one of the [more complete](https://wiki.osdev.org/C_Library#Musl) implementations.

System calls can be implemented as needed to emulate the expected behavior.