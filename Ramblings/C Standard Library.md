If I want to run C or C++ code on Perception, I need to port a C Standard Library. Without a C Standard Library, we can't even `#include <stdlib.h>`.
Without a C Standard Library, we can't even port a C++ Standard Library (which calls the C Standard Library under the hood for things like IO and threading).
I want both a C and C++ Standard Library, because it'll make life so much easier.

There are [many options](https://wiki.osdev.org/C_Library) when it comes to a picking a library to port to Perception.
Many of the smaller ones lack a lot of functionality (some don't even have a `math.h`) and I want to make it as easy as possible to port software to Perception, so that limits myself to one of the big players.
I eventually settled on [musl](http://musl.libc.org/).

musl depends on calling the Linux system calls.
But, I saw Emscripten, which runs inside of a web browser and has a totally different interface with it's environment than Linux's system calls, ported musl, so it must be possible to port it to my microkernel environment.
I ended up building a 
[Linux system call shim](https://github.com/AndrewAPrice/Perception/tree/master/Libraries/Linux%20System%20Call%20Shim).
I started by writing a small script that looped over all of the Linux system calls, and generated a giant switch statement based on the system call number, and a .cc/.h for each system call that printed a message saying it was unimplemented.
From there, I created a `printf("Hello world\n");` and watched what system calls it complained were missing. I only needed a few to get `printf` working.

My next step was to port the C++ Standard library ([libcxx](https://libcxx.llvm.org/)).
I took some debugging to that ended up being fixed by requiring me to fix my linker script to support global constructors/destructors.
I also had to change my build system so the libcxx headers were included before musl.
Then, I played around with `std::thread`, `std::function`, and lambdas and implemented the missing system calls in my shim.

For many of the Linux system calls, such as allocating memory or creating a thread, I was able to implement it by issuing one of my kernel's system calls.
But for others, especially the ones involving file IO, I had to emulate the behavior by issuing RPCs to services or otherwise just in code (for example, our `File::Read` takes an offset into the file with each call, so in the shim we keep track of open files and the current read position, and `lseek` doesn't do anything other than update a counter).
