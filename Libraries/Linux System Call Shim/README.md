A shim converting Linux system calls to using Perception system calls and services.

[musl libc](https://musl.libc.org/) is built around the Linux system calls. Rather than modify or create our own [C standard library](https://en.wikipedia.org/wiki/C_standard_library), we're using musl because it is one of the [more complete](https://wiki.osdev.org/C_Library#Musl) implementations.

We can implement system calls as needed and emulate the expected behaviour.