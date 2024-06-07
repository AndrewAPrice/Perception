# Debugging Tips, Tricks, and Notes

The below instructs work much better if built with `--dbg`.

## Decompiling a file

We can use the followig command to decompile an compiled file.

`objdump -M intel -C -D -l  <.app> > dump.txt`

## Debugging with GDB

Make sure you have a GDB installed that supports x86_64-elf. On Mac, you can install `x86_64-elf-gdb` with `brew`.

Build with `./build all --dbg`. Then run the emulator manually with

`qemu-system-x86_64  -boot d -cdrom ../Perception.iso -m 512 -serial stdio -s -S`

Open GDB. Add the symbols of the kernel:

`symbol-file ../fs/boot/kernel.app`

Add the symbols of the application you want to debug, e.g.:

`add-symbol-file ../fs/Applications/Launcher/Launcher.app`

Then connect to qemu:

`target remote localhost:1234`

Find the exception/interrupt number in `exception_messages` in [Kernel/sources/exceptions.c]. Set it as a breakpoint in the format of `break isr<exception number>`. For example, breaking on a general protection fault would be:

`break isr13`

Then start Perception:

`c`

We're now in kernel land, but we need to restore the userland state.

If the exception has an error code (see Kernel/sources/exceptions.asm]), pop it off the stack:
`set $rsp = $rsp + 8`

Then restore the userland state with:

```
set $rip = *(unsigned long long *)($rsp)
set $rsp = *(unsigned long long *)($rsp + 24)
```

Now you're at the point where the exception occured in userland. You can use normal gdb commands to inspect the source code. For example, you can print a backtrace with:

`bt`

## Inspecting core dumps in VSCode

Install the `CodeLLDB` extension in Visual Studio Code.
