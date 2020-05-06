# Shovel

Shovel is compiler for a subset of Javascript, which is a dynamically typed high level language. Shovel compiles to Turkey assembly language, which runs on the Turkey virtual machine. Tools related to Shovel go in here. The assembler is also here (rather than the Turkey directory which is exclusively the virtual machine) because it is part of the build process.

# Status
- The assembler is done.
- The compiler is done. There are no optimizations yet.
- See the Turkey directory for progress on the virtual machine.

# Documentation
- [The assembler and its usage.](Assembler.md)
- [The grammar supported by Shovel.](Grammer.md)

# Features
The following Javascript features are supported:

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

- Constant objects. A constant object would be object where you can read but cannot set the properties. You can read variables, call functions, but you can't set any properties. Any object can be cast as a constant object, but it cannot be cast back. This would make it safe to pass objects (especially objects that act as interfaces) to untrusted subcomponents that are allowed to call, but not touch, the object.
- Functional calls. A functional call is a call to a function that has no side effects. Its output is entirely dependent on its input. The compiler can completely optimize away functional calls or combine them. How can the runtime or compiler determine if a function is purely functional in a dynamically typed language? Perhaps the responsibility lies on the programmer to tell us which function calls are functional, e.g.: a standard function call is myFunc(a, b), but a functional call could be myFunc(|a, b|).
- Exceptions. Some way of catching runtime errors, as well as errors thrown by the code itself.
