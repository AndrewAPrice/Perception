# Perception

Perception is a hobby operating system. There are multiple components to this project. Shovel is a programming language being developed for Perception. Turkey is
the virtual machine which runs the compiled Shovel code. Perception is an operating system built around Turkey, to run Shovel programs natively.

Shovel and Turkey are being developed seperately from Perception, so that they may be embedded into any system or application.

## Directory Structure
- fs - The root file system.
- shovel - Tools related to the Shovel language build system - compiler, assembler
- turkey - The Turkey virtual machine
- perception - The Perception kernel will eventually go in here.

## Todo
This is a very high level to-do list. Near goals:
- Optimize SSA (constant propagation, deadcode elimination).
- Add classes to compiler/assembler/VM (for polymorphic inline chaching).
- Implement basic block versioning JIT.
- Change C API to work with JIT.

Eventual goals:
- Start work on Perception and get Turkey VM running bare metal.
- Port the build tools to Shovel.
- Build a Shovel IDE in Perception (requires adding a debugging API to Turkey).

Long term goals:
- Make a great OS.
