During my adventures to port fontconfig (a dependency of Skia) to Perception, I found that I was very slow.
Unless you have a cached file (and Perception only has read only file system support right now) fontconfig opens every font it can find and parse them.
I only have 22 fonts, yet fontconfig was taking minutes.
Every application would have to spend minutes starting up just to be able to draw text.
This is unacceptable.

Perception uses musl, and musl reads into a 1024 byte buffer when you read from a file.
However, musl's implemention of seeking clears the buffer, even if you're only seeking a few bytes ahead.
I logged the reads and seeks and saw there was a lot of back and forth jumping as fontconfig/freetype try to parse the fonts.
For example, we'd read 16 bytes from position 172, seeking to 270568 to read 4 bytes, then seeking back to position 188.
Or, we'll read 32 bytes from 291146, then seek 19 bytes ahead from 291178 to 291197.
Another time, we seek to 444, read 2 bytes, see we seek to 454, read 2 bytes, seek to 462, read 6 bytes, seek to 468, read 6 bytes. etc.
Approx 2446 times we try to read 2 bytes from position 270660.

Basically, fontconfig/freetype would try to read a few bytes, but musl would read 1024 bytes to fill the buffer, then fontconfig would seek a few bytes ahead causing musl's buffer to clear, triggering musl to read another 1024 bytes to fill the buffer.

Then, I noticed in [freetype's documentation](https://freetype.org/freetype2/docs/design/design-4.html):

> As an example, the default implementation of streams is located in the file src/base/ftsystem.c and uses the ANSI functions fopen, fseek, and fread. However, the Unix build of FreeType 2 provides an alternative implementation that uses memory-mapped files, when available on the host platform, resulting in a significant access speed-up.

With memory mapped files, when files are opened they get mapped into the virtual address space of a process.
The file isn't actually copied into memory yet, instead it gets lazily loaded in pages (Perception uses 4kb pages) when accessed.
When the program tries to read the memory where the file if supposed to be, we get a page fault because there's no memory mapped to that address.
The thread temporarily freezes while the rest of the operating system loads the page from disk and into memory.
Once loaded, the thread unfreezes and continued executing unaware that anything happened.

How does this work with a microkernal that has no understanding of what a file is?

Perception has had shared memory for a while.
We can also pass shared memory around in Permebufs 
This is how we pass the buffer that we want to copy bytes into the device driver so it can read from disk, and how we tell the window manager that this is the contents of our window.

To support memory mapped files, I extended the shared memory interface to do a few things:
* Allow shared memory to be lazily allocated.
* Intercept page fault and send threads to sleep when an unallocated page is accessed.
* Send the creator of the shared memory a message when a not yet allocated page is trying to be accessed.
* Allow the creator time to allocate and prepare the memory, then alert the kernel when it's ready so it can wake up any threads waiting for that block of memory.
* Allow other processes to access the memory as read-only.
* Give a way to see if shared memory is not lazily allocated (so a malicious program can't being down a device driver by saying "write into this buffer, and I'll make you hang by never allocating it").

I implemented all of the above.
I didn't need [that much code](https://github.com/AndrewAPrice/Perception/blob/master/Applications/Storage%20Manager/source/memory_mapped_file.cc) in my Storage Manager service.
Fontconfig sped up from minutes to seconds to parse all of the fonts on disk.

It still seemed silly that every graphical application had to parse that many files on each launch.
I saw that fontconfig just provides an interface for enumerating and matching fonts, so I did the microkernel thing and put fontconfig behind it's own service called [Font Manager](https://github.com/AndrewAPrice/Perception/tree/master/Applications/Font%20Manager) that exposes font matching and enumeration but calls fontconfig under the hood.
Now, the seconds of work only has to be done once and every other application can just ask the Font Manager to match them a font.
