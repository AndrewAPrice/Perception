# Turkey
Turkey is a stack-based virtual machine. It is designed to run the Shovel language, and to be portable to be embedded into applications and even run on bare metal (for the Perception OS.)

## Files
- turkey.h - The Turkey VM header file.
- hooks.h - Hooks into the system. Implement this if you want to port Turkey to a new platform.

## Subdirectories
- Windows - Windows specific code in here, and a Visual Studio project. There is a main.cpp and hooks.cpp showing how you can use the Turkey VM standalone. Use the 'Test' configuration in Visual Studio to compile Turkey as a standalone program instead of a library.

The Turkey assembler (that compiled Turkey assembly into Turkey bytecode) is found in the Shovel folder (despite being part of Turkey not Shovel) because the rest of the build tools are in there. This folder is exclusively for the Virtual Machine - it's a cleaner directory structure.

See shovel/Assembler.md for information on the VM's instructions, opcodes, and the basics of how it runs.

## Debugging
Turkey will eventually have two modes that the VM can run in (these can be specified during initialization):
- Debugging mode: This version interpretes the bytecode. Designed for Shovel code that has been compiled and assembled with the debugging flag. This version will be noticeably slower, but we will be able to set breakpoints, single step through code, know exactly what line of code we are executing from the origional source code, and the value of our variables.
- Non-debugging mode: This version ignores debugging information and JIT compiles code to native modes.
