The file contains the layout of immediate representation used by the Turkey Virtual Machine.

# Instructions
The immediate representation is in two-address code. The destination is the actual position of the instruction.

Opcode|Instruction|Operand 1|Operand 2|Notes
------|-----------|---------|---------|-----
&nbsp;|**Math**|||
0 | Add | A | B | Add A and B together. Calls the property "+" on an object.
1 | Subtract | A | B | Subtracts B from A. Calls the property "-" on an object.
2 | Divide | A | B | Divides A by B. Calls the property "/" on an object.
3 | Multiply | A | B | Multiplies A and B togther. Calls the property "*" on an object.
4 | Modulo | A | B | Divides A by B and returns the remainder. Calls the property "%" on an object.
5 | Invert | A | | Inverts the sign of A.
6 | Increment | A | | Increments A by one. Calls the property "++" on an object.
7 | Decrement | A | | Decrements A by one. Calls the property "--" on an object.
&nbsp;|**Bit Operations**|||
8 | Xor | A | B | Performs a bitwise exclusive or. Calls the property "^" on an object.
9 | And | A | B | Performs a bitwise and. Calls the property "&" on an object.
10 | Or | A | B | Performs a bitwise or. Calls the property "|" on an object.
11 | Not | A || Performs a bitwise not/ Calls the property "!" on an object.
12 | ShiftLeft | A | B | Performs a bitwise shift left. Calls the property "<<" on an object.
13 | ShiftRight | A | B | Performs a bitwise shift right. Calls the property ">>" on an object.
14 | RotateLeft | A | B | Performs a bitwise rotation left. Calls the property "<<<" on an object.
15 | RotateRight | A | B | Performs a bitwise rotation right. Calls the property ">>>" on an object.
&nbsp;|**Logical Comparison**|||
16 | IsNull | A || Returns true if A is null, false otherwise.
17 | IsNotNull | A || Returns true if A is not null, false otherwise.
18 | Equals | A | B | Returns true if A and B are equal, false otherwise.
19 | NotEquals | A | B |Returns true if A and B are not equal, false otherwise.
20 | LessThan | A | B | Returns true if A is less than B, false otherwise. Calls the property "<" on an object.
21 | GreaterThan | A | B | Returns true if A is greater than B, false otherwise. Calls the property ">" on an object.
22 | LessThanOrEquals | A | B | Returns true if A is less than or equal to B, false otherwise. Calls the property "<=" on an object.
23 | GreaterThanOrEquals | A | B | Returns true if A is greater than or equal to B, false otherwise. Calls the property >= on an object.
24 | IsTrue | A || Returns true if A is not zero or null, false otherwise.
25 | IsFalse | A || Returns true if A is is zero or null, false otherwise.
&nbsp;|**Variable Manipulation**|||
26 | Phi | A | B | Variable may be A or B.
27 | LoadClosure | Closure || Loads a closure.
28 | StoreClosure | Closure | A | Saves A into a closure.
&nbsp;|**Arrays**|||
29 | NewArray | Size || Create an array with the size preallocated.
30 | LoadElement | Key | Array | Returns an element from an object (where [key] is the property), an array (where [key] is an index), or a character as an integer in a string (where [key] is the position of the character). Returns null if the key cannot be found.
31 | SaveElement | Key | Array | Saves an element (the pushed parameter) into an object (where [key] is the property) or an array (where [key] is an index.) This resizes an array or creates a new property on an object if the key does not already exist, otherwise it overwrites the currently assigned value for that key.
&nbsp;|**Objects**|||
32 | NewObject || Creates an object.
33 | DeleteElement | Key | Object | Delets an element from an object (where [key] is the property). Does nothing if the element does not exist.
&nbsp;|**Buffers**|||
34 | NewBuffer | Size || Create a buffer, where [size] is the number of bytes the buffer should be.
35 | LoadBufferUnsigned<8> | Address | Buffer | Loads an 8-bit unsigned integer from a buffer at [address] bytes in.
36 | LoadBufferUnsigned<16> | Address | Buffer | Loads a 16-bit unsigned integer (little endien) from a buffer at [address] bytes in.
37 | LoadBufferUnsigned<32> | Address | Buffer | Loads a 32-bit unsigned integer (little endien) from a buffer at [address] bytes in.
38 | LoadBufferUnsigned<64> | Address | Buffer | Loads a 64-bit unsigned integer (little endien) from a buffer at [address] bytes in.
39 | StoreBufferUnsigned<8> | Address | Buffer| Saves an 8-bit unsigned integer (the pushed parameter) to a buffer at [address] bytes in.
40 | StoreBufferUnsigned<16> | Address | Buffer | Saves a 16-bit unsigned integer (the pushed parameter - little endien) to a buffer at [address] bytes in.
41 | StoreBufferUnsigned<32> | Address | Buffer | Saves a 32-bit unsigned integer (the pushed parameter - little endien) to a buffer at [address] bytes in.
42 | StoreBufferUnsigned<64> | Address | Buffer | Saves a 64-bit unsigned integer (the pushed parameter - little endien) to a buffer at [address] bytes in.
43 | LoadBufferSigned<8> | Address | Buffer | Loads an 8-bit signed integer (two's compliment) from a buffer at [address] bytes in.
44 | LoadBufferSigned<16> | Address | Buffer | Loads a 16-bit signed integer (two's compliment, little endien) from a buffer at [address] bytes in.
45 | LoadBufferSigned<32> | Address | Buffer | Loads a 32-bit signed integer (two's compliment, little endien) from a buffer at [address] bytes in.
46 | LoadBufferSigned<64> | Address | Buffer | Loads a 64-bit signed integer (two's compliment, little endien) from a buffer at [address] bytes in,
47 | StoreBufferSigned<8> | Address | Buffer | Saves an 8-bit signed integer (the pushed parameter - two's compliment) to a buffer at [address] bytes in.
48 | StoreBufferSigned<16> | Address | Buffer | Saves a 16-bit signed integer (the pushed parameter - two's compliment, little endien) to a buffer at [address] bytes in.
49 | StoreBufferSigned<32> | Address | Buffer | Saves a 32-bit signed integer (the pushed parameter - two's compliment, little endien) to a buffer at [address] bytes in.
50 | StoreBufferSigned<64> | Address | Buffer | Saves a 64-bit signed integer (the pushed parameter - two's compliment, little endien) to a buffer at [address] bytes in.
51 | LoadBufferFloat<32> | Address | Buffer | Loads a 32-bit floating point number from a buffer at [address] bytes in.
52 | LoadBufferFloat<64> | Address | Buffer | Loads a 64-bit floating point number from a buffer at [address] bytes in.
53 | StoreBufferFloat<32> | Address | Buffer | Saves a 32-bit floating point number (the pushed parameter) to a buffer at [address] bytes in.
54 | StoreBufferFloat<64> | Address | Buffer | Saves a 64-bit floating point number (the pushed parameter) to a buffer at [address] bytes in.
&nbsp;|**Signed Integers**|||
55 | SignedInteger | Value [low] | Value [high] | A constant signed integer.
56 | ToSignedInteger | A || Converts A to a signed integer. Arrays, objects, and strings are converted to the number of elements they have.
&nbsp;|**Signed Integers**|||
57 | UnsignedInteger | Value [low] | Value [high] | A constant unsigned integer.
58 | ToUnsignedInteger | A || Converts A to an unsigned integer. Arrays, objects, and strings are converted to the number of elements they have.
&nbsp;|**Floating Point Numbers**|||
59 | Float | Value [low] | Value [high] | A constant floating point number.
60 | ToFloat | A || Converts A to a floating point number. Arrays, objects, and strings are converted to the number of elements they have.
&nbsp;|**Booleans**|||
61 | True ||| A constant true value.
62 | False ||| A constant false value.
&nbsp;|**Nulls**|||
63 | Null ||| A constant null value.
&nbsp;|**Strings**|||
64 | String | Value [low] | Value [high] | A constant string, where [value] is a pointer into the string table.
&nbsp;|**Functions**|||
65 | Function | Function [low] | Function [higher]| Creates a function pointing, where [function] is a pointer to the function.
66 | CallFunction ||| Calls a function. The parameters must be immediately pushed before calling this. If you do not push enough parameters, they will appears as null values to the calling function, and if you push too many - they will simply be ignored. If a function does not return anything, it is equivilent to returning null.
67 | CallFunctionNoReturn ||| Calls a function without wanting anything to be returned. The parameters must be immediately pushed before calling this. If you do not push enough parameters, they will appears as null values to the calling function, and if you push too many - they will simply be ignored.
68 | Push | A || Pushes A to an imaginary stack. This imaginary stack is simply for instructions that immediately follow - such as function calls, saving elements, and writing to buffers.
&nbsp;|**Types**|||
69 | GetType | A || Returns a stirng specifying what type A is.
&nbsp;|**ControlFlow**|||
70 | Jump | Label || Jumps execution to [label].
71 | JumpIfTrue | Label | A | Jumps execution to [label] if A is true.
72 | JumpIfFalse | Label | A | Jumps execution to [label] if A is false.
73 | JumpIfNull | Label | A | Jumps execution to [label] if A is null.
74 | JumpIfNotNull | Label | A | Jumps execution to [label] if A is not null.
&nbsp;|**Loading**|||
75 | Require | [module] | Loads a the module called [module] and calls its first function.
