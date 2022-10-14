Perception uses a [custom build system](https://github.com/AndrewAPrice/Perception/tree/master/Build).

C++ build systems seem so complicated, but they shouldn't be.
There's no reason I can't just have a folder of C++ source code, and tell the build system to build it, with very minimal configuration from me.
I started with this principal, and went with it.

I want my code to be nice and organized, and so there are 3 top level directories:
* Kernel - the kernel
* Applications - end binaries that can be ran
* Libraries - code that can be shared between libraries

Each project (other than the kernel) lives in its own directory and has a `metadata.json` file to configure how it's built.
For most programs, `metadata.json` should be super simple and just list the libraries they depend on:
```
{
  "dependencies": {
    "musl",
    "libcxx",
    "perception",
    "perception ui"
  }
}
```

Then the build system can just scan through every source code file in `Source` subdirectory and attempt to build it.
Maybe some applications and libraries have an `Assets` folder of files that should be distributed with it.
Compilers can output the dependencies of files (all of the header files you included), which a build system can use to track if anything has been recently touched to tell if we need to recompile a particular source code file.

Why would most programs need anything more complicated? Some are more complicated (third party libraries I'm porting to Perception), and so the `metadata.json` has the ability to say to ignore building these files, or define these symbols. This has been sufficient to be able to build musl, libcxx, Skia, freetype, and other libraries.

A build system should handle circular dependencies.
The fact that C/C++ has headers is so `a.c` can declare its functions in `a.h`, and `b.c` can declare its functions in `b.h`, and `a.c` can include `b.h` and `b.c` can include `a.h`. The order in which you build `a.c` or `b.c` does not matter.
In practice, musl (the C Standard Library) depends on my Linux System Call Shim, that is written in C++ and depends on the perception library to communicate with services, which depends on libcxx, which depends on musl.
The order in which any of them are built doesn't matter, as long as they can include each others headers, and all get linked into the final binary.

The exception where you have to compile things in order is when source code generation is involved.
For example, [Permebufs](https://github.com/AndrewAPrice/Perception/blob/master/Build/Permebuf.md) need to be transpiled to C++ before anything including the generated C++ headers can compile.
Interestingly enough, if we had to build tools to generate the source code, the tools would have to be compiled for the host OS doing the build and not the target OS (Perception), so it would be unlikely that we could reuse the compiled object files/binaries later.
