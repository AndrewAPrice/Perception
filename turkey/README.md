# Turkey
Turkey is a stack-based virtual machine. It is designed to run the Shovel language, and to be portable to be embedded into applications and even run on bare metal (for the Perception OS.)

## Files
- turkey.h - The Turkey VM header file.
- hooks.h - Hooks into the system. Implement this if you want to port Turkey to a new platform.

## Subdirectories
- Windows - Windows specific code in here, and a Visual Studio project.

The Turkey assembler (that compiled Turkey assembly into Turkey bytecode) is found in the Shovel folder (despite being part of Turkey not Shovel) because the rest of the build tools are in there. This folder is exclusively for the Virtual Machine - it's a cleaner directory structure.

See shovel/Assembler.md for information on the VM's instructions, opcodes, and the basics of how it runs.

## Todo
- Objects
- Functions
- Meta properties for objects, elements, and buffers
-- array.resize(newsize)
-- array.allocate(newamount)
-- array.length
-- array.insert(index, val)
-- array.remove(start, count)
-- array.splice(start, count)
-- buffer.clone()
-- buffer.dispose()
-- buffer.length
-- buffer.resize(newsize)
-- object._get(property)
-- object._propeties
-- string.length
-- string.substring(start, count)
