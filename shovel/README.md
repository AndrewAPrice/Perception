# Shovel

Shovel is a dynamically typed high level language. Shovel compiles to Turkey assembly language, which runs on the Turkey virtual machine. Tools related to Shovel go in here. The assembler is also here (rather than the Turkey directory which is exclusively the virtual machine) because it is part of the build process.

# Status
- The assembler is done.
- The compiler is done. There are no optimizations yet.
- No virtual machine yet.

# What's all this Javascript?

A lot of these tools are written in Javascript. I wrote these tools in Javascript (specifically to run under Node.js) because Javascript is very close in syntax and functionality to Shovel. Once the toolchain and virtual machine are mature enough, I plan to port these tools natively to Shovel, creating a self hosting system.

I try to avoid using Javascript features (like prototypes) that are currently not planned for Shovel.

# Documentation
Check out the MD files for documentation.

# Features
Shovel is a dynamically typed high level language.

* Dynamically typed.
* Floating point and signed and unsigned integers.
* Native string objects.
* Bitwise operations.
* Closures.
* First class functions.
* Dynamic arrays.
* Objects.
* Memory buffers that provide byte-specific access.
* Short circuit operators (&& ||).
* Basic control structures: for, foreach, do..while, while, if, continue, break, goto
* jSON syntax for specifying objects and arrays.
* A module-based include system.

## Object system
Shovel has no classes. Instead, we can use a combination of objects and closures to simulate the same functionality. This is a simple example:

````
var createAnObject = function() {
	// private fields, ones we access inside inner functions stay alive because they become closures
	var somePrivateField = 0;

	// public fields
	someObject = {};
	someObject.getPrivateField = function() {
		// we will modify the variable in the parent function
		return somePrivateField++;
	};
	someObject.somePublicField = 120;

	// return our object
	return someObject;
};

var myObject = createAnObject();
myObject.getSomePrivateField(); // returns 0
myObject.getSomePrivateField(); // returns 1
myObject.getSomePrivateField(); // returns 2
myObject.somePublicField; // returns 120

myObject.somePrivateField; // returns null, since it's not part of myObject
myObject.somePrivateField = "ABC"; // but you can add it!
````

## First class functions
Because functions are first class objects, we can do event based programming easily. We can pass functions as parameters and assign them to variables.

createButton("Click Me", function(event) {
	// the user clicked the button!
});

## Future Features
What features should be in future versions of Shovel?

Shovel is dynamically typed, this makes it tough to optimize. The language right now is very simple, but it's also very flexibly.

We could add a type system, this would make it much easier to optimize, especially by an eventual JIT compiler.
