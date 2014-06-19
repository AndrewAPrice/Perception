This document describes how Turkey [will] just-in-time compiles code.

JIT compilation translates Turkey bytecode into machine code while running, in contrast to intepreting the bytecode.

JIT compilation is enabled if you set the 'debug' property of TurkeySettings to 'false' that you pass into turkey_init(). Setting the 'debug' property to 'true' will result in the bytecode being interpreted instead of JIT compiled (which may be your intention if you're trying to debug and application you have developed.) This document describes how the Turkey JIT compiler works internally.

# Overview
Turkey bytecode is dynamically typed, which is a result of Shovel being a dynamically typed language. Attempting to add two variables together can perform vastly different operations - appending two strings together, calling the '+' property of an object, adding two floats together, adding to integers together, or it could be an invalid operation (such as adding two functions together). To solve this problem, Turkey uses basic block versioning.

# Single Static Assignment
Turkey converts bytecode into single static assignment form when the bytecode is loaded.

# Basic Block Versioning
This is not the same method as the basic block versioning described in the paper 'Removing Dynamic Type Tests with
Context-Driven Basic Block Versioning' http://pointersgonewild.files.wordpress.com/2011/08/bbv_paper.pdf
