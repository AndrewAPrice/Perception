This document describes how Turkey will just-in-time compile code. I am currently working on implementing this design.

JIT compilation translates Turkey bytecode into machine code while running, in contrast to intepreting the bytecode.

JIT compilation is enabled if you set the 'debug' property of TurkeySettings to 'false' that you pass into turkey_init(). Setting the 'debug' property to 'true' will result in the bytecode being interpreted instead of JIT compiled (which may be your intention if you're trying to debug and application you have developed.) This document describes how the Turkey JIT compiler works internally.

# Overview
Turkey bytecode is dynamically typed, which is a result of Shovel being a dynamically typed language. Attempting to add two variables together can perform vastly different operations - appending two strings together, calling the '+' element of an object, adding two floats together, adding to integers together, or it could be an invalid operation (such as adding two functions together). To solve this problem, Turkey uses basic block versioning.

# Single Static Assignment
Turkey converts bytecode into single static assignment form when the bytecode is loaded. SSA form is represented as one/two-instruction addresses, with the 'destination' being the instruction's location, for example:
```
  0: Integer 5
  1: Integer 10
  2: Add [0] [1]
```
There a various optimizations that are done by looking at the single static assignment form - constant propagation, dead code elimination, and common sub-expression elimination.

SSA form is not stored out to bytcode - because a) it can be larger - all SSA form instructions are 9 bytes, while bytecode instructions can a small as one 1 byte, and b) SSA form is unsafe - it's designed to be fast to compile into machine code, and so many checks that are present when loading and parsing bytecode are not done with SSA form (and we can avoid doing them - as we can treat the compiler as a trusted source - but if they came from a file, we cannot assume the same.)

You can print out the SSA form by compiling ssa.cpp with PRINT_SSA defined. However, PRINT_SSA introduces a dependency on stdio.h (for printf) which may not be avaliable on bare metal platforms.

# Assignmnt-Time Basic Block Versioning
Being a dynamic language, not all types are known when generating SSA form - particularly types of parameters, when we read an element of an aggregate object, and when function calls return a value. There are two things we want to avoid doing - a) having to pass around types - because we have limited registers and memory access is slow, so we would prefer to be able to remove having to pass around types all of the time - and avoiding having to encode it into high-bits of registers or something, allowing us to have the entire width of the register for storing values, and b) type checking everytime we perform an operation, because there are 121-combinations of types for two address instructions - with most combinations having different outcomes.

A basic block is a small portion of code with only one entry point, and one exit point (although that exit point may be a branch) - and execution within a basic block flows linear. Turkey generates a new basic block every time: a jump(or the destination of a jump) is encountered, we read an element of an aggregate, or we call a function and use the return result. A basic block has a set of 'input' values which are either parameters passed into the function (for a function's entry point/first basic block) or a passed from a jump from a previous basic block.

This allows us to make assumptions about basic blocks; a) execution flows linearly though a basic block, b) all types remain constant throughout executing a basic block. We can check the types of the input values when entering a basic block, and never have to perform type checking while actually executing a block.

When we start executing a basic block, we can see what 'version' of the basic block we need to execute (with versions differing based on the combination of parameter types). If a version of a basic block doesn't exist yet, we can compile it into native code - if it does exist, we can jump to the already-compiled native code. As basic block versions are only created and compiled on demand when they are needed, uncalled basic block versions are never compiled.

We can move the type checking from the start of the basic block (the 'jumpee') to the end of the previous basic block (the 'jumper'). In most cases, we know the types of all values (due to the version of the basic block we're executing) that is flowing into and out of a basic block, so rather than doing type checking at runtime when we are about to jump to the next basic block, we can actually lookup the version of the basic block we're about to jump to, then replace that lookup to a jump directly to that version, avoiding the need to check any types. The only exceptioons to this are two special cases - when we read an element from an aggregate type, or we hold onto a value return by a function. In these case, we only have need to use the type returned to figure out which version of the next basic block to use, then we can forget about the type. There's no need to keep a copy of types in registers or to pass them around, as the types are hardcoded into the version of the basic block being executed.

This results in there being four special cases when we need to perform a type test at runtime;

* If a version/code-path of basic block has never been executed before (but it's then filled in with a jump to the specific version of a basic block so this test never has to run in the same location again.)
* At the start of a function call, to figure out what version of the first basic block to call.
* When we read an element from an aggregate type.
* We call a function and hold on to it's return value.

This is not the same method as (but was inspired by) the basic block versioning method described in the paper 'Removing Dynamic Type Tests with Context-Driven Basic Block Versioning' http://pointersgonewild.files.wordpress.com/2011/08/bbv_paper.pdf

There is a potential for the number of possible permutations of a basic block to explode. If this becomes an issue (when the versions of a basic block grows exponentially) I may have to think up a solution, but I suspect that in most real world scenarios this would be a non-issue because function calls and code paths will tend to be highly predicatable and repeatative.

If this does become an issue (which the programmer is likely to notice due to memory usage exploding), and it only happens very rarely, the best solution may be to make the programmer aware of it, and possible best coding practices to avoid this. Again, I think this would be a very rare scenario that versions will grow exponentially

# Function Calls
When we call functions and pass parameters into a function, we also need to tell function the number of parameters we've passed in and the type of each parameter. When we enter a function, we'd like to perform as few checks as possible, to figure out what version of the first basic block to call.

There are 10 different types of values (nulls, booleans, unsigned ints, signed ints, floats, objects, arrays, memory buffers, function pointers, strings), so the type can be encoded in 4 bits, with 6 values reserved for internal use or future types. In a 64-bit register, we describe the value of 16 types. (If we encoded the type information in base-10 instead of binary, we could actually describe 19 types in a single 64-bit register instead. If in the future we had 11 types, 12, or 13 types or more. The algorithm for figuring out how many values we could store in a register is log t(2^r) - where t is the number of types, and r is the number of registers.) If more than 16 parameters are passed to a function, we can use additional registers, but in most cases a single register would be sufficient.

We know all of this at the time of compiling the caller's basic block, so we can hardcode in the encoded types into machine code and don't actually have to calculate this type encoding at runtime.

The actual parameters to a function call are - the closure pointer, the number of parameters, the encoded parameter types, and the parameter values. These actual parameters are passed in the general purpose registers first, with additional values spilling onto the stack. The caller is responsible for cleaning up the stack upon returning, and restoring it's own values. The callee returns the return value in two registers - one of the value, and one for the type.

If not enough parameters are passed into a function, the missing parameters are assumed to be null. Likewise, if too many values are passed into a function, they are simply considered nulls. Trailing nulls are always trimmed off of the end of parameters, since they're not transfering a value and is identical to not passing a parameter in at all.
 
# Polymorphic Inline Caching

# Benchmarking
As new features are added and improvements are made to the virtual machine and JIT compiler, I would like to run benchmarks. I would appreciate it if anyone knows of any programming language benchmarks that test only the language itself that I can use to test the performance of Turkey. If you do - please contact me at messiahandrw - at - gmail - dot - com.
