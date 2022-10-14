A port of [musl libc](https://musl.libc.org/) to Perception, an implementation of the [C standard library](https://en.wikipedia.org/wiki/C_standard_library).

[Background story about porting musl.](https://github.com/AndrewAPrice/Perception/blob/master/Ramblings/C%20Standard%20Library.md)

Fun facts:
- The public include folder in this directory is called 'include/' instead of the Perception-standard 'public/' because many of the source files reference "../../include/" in the #include path.
- musl is built around Linux's system calls. We (incompletely) emulate Linux's system calls in a [shim](../Linux System Call Shim/README.md).
