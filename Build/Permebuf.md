# Permebufs
Permebufs are **Per**ception **me**mory **buf**ers.

Permebufs are a data interchange and storage format used throughout the Perception operating system.

A permebuf consists of a root message, which may contain children, and the messages are defined in .permebuf files. These files live in a package's `Permebuf` subdirectory.

## Permebuf definition language.

### Comments
Permebuf files can contain line comments `//` and block comments `/* */`.

### Import other files

You can import other permebuf files with:

```
import "<filename>.permebuf"
```

You can also import permebuf files from other libraries with:

```
import <<LibraryName>/<filename>.permebuf>
```

### Namespaces

Permebuf files can exist in a namespace.

```
namespace <namespace>;
```

Namespace can be nested with `.`.

### Messages

```
message <name> {
	<fields>
}
```

Messages can be nested.

#### Message field
Message fields can be defined as:

```
<name> : <type> = <field number>;
```

Field numbers start at 1, they must be unique within a message, and there must not be gaps.

#### Backwards compatibility
Messages are backwards and forwards compatible, if the following conditions are true:

* Fields can be added.
* Existing fields can be renamed, but their type or field number can never change.
* Old fields cannot be deleted, but if you don't want anyone writing or reading them, they can be marked as 'reserved'.
* Messages, enums, and oneofs can be renamed.
* Existing fields can be shuffled around in the text file.
* Function names can change (and the request and response message names can also be renamed), but function numbers can't be changed.

#### Reserved fields

If you wish to maintain backwards and forwards compatibility with a message, but want the closest thing to removing a field to avoid anyone from reading and writing to it, you can 'reserve' a field:

```
reserve <type> = <field number> {, <field number>};
```

You can group together multiple fields with the same type in the same statement.
* Enum fields should have their types converted to `enum` rather than the enum name.
* Oneof fields should have their types converted to `oneof` rather than the oneof name.
* `list`, `array` or message fields should have their types converted to `pointer`.

These fields still take up space, however. So design your APIs wisely to anticipate future changes.

### In-built types
The built in types are:

* `bytes` - raw byte data.
* `string` - a string.
* `bool` - a one boolean. Multiple booleans in the same file are compressed together into bit fields.
* `uint8` - An unsigned 8-bit integer.
* `int8` - A signed 8-bit integer.
* `uint16` - An unsigned 16-bit integer.
* `int16` - A signed 16-bit integer.
* `uint32` - An unsigned 32-bit integer.
* `int32` - A signed 32-bit integer.
* `uint64` - An unsigned 64-bit integer.
* `int64` - A signed 64-bit integer.
* `float32` - A 32-bit floating point number.
* `float64` - A 64-bit floating point number.
* `list<type>` - A linked list of `type`.
* `array<type>` - An array of `type`.
* `pointer` - Only used for reserved fields that may have been a list, array, or pointer.
* `enum` - Only used for reserved fields that may have been an enum.
* `oneof` - Only used for oneof fields that may have been an enum.
* `SharedMemory` - A shared memory buffer.

### Enums
Enums are defined as:
```
enum Enum {
	<fields>
}
```

A enum field is defined as:

```
<value name> = <value number>;
```

Enums can have values from 0 to 65,535. Each enum value must be unique to the enum.

Enums can be nested inside of messages.

### Lists and arrays
Lists and arrays are very similar. Arrays have a fixed length at construction time, but have constant time lookup and length calculations, while lists can be dynamically added to, but have linear lookup and length calculations.

### Oneof

Oneofs share a single memory pointer, with the limitation that only one of the oneof field's can be set at any time. They are defined as:

```
oneof <oneof name> {
	<oneof option>
}
```

A oneof option is defined as:

```
<name> : <type> = <option number>;
```

The type can be any pointer type, which means a `list`, `array`, `string`, `bytes`, or Message.

Each entry needs to have an option number which is unique within the oneof block, however it may not be 0 and the unknown name can't be "Unknown" (which is a special name/number indicating that no oneof value is set.)

Oneofs can be nested inside of messages.

### Mini-messages
Mini-messages are messages that have a fixed size up to 32 bytes. Mini-messages are defined as
```
minimessage <name> {
	<fields>
}
```
Only primitive types are allowed. Lists, arrays, strings, messages, oneofs, other mini-messages are not allowed. Bytes are allowed, but they need to specify a size, e.g. `bytes<10>` for a 10 byte array.

### Services
A service is a collection of exposed functions. Processes can create instances of services that other processes can all. This forms the basis of cross-process function calling.
```
service <name> {
	<function>
}
```

The name is joined with the current namespace with '.' as the delimiter. The full name can not be more than 88 characters long.


### Function
A function can be defined inside of a service as:
```
<name> : <request message type> [-> <response message type>] = <function number>;
```

The name and number must be unique inside of the message. The maximum function number is 2305843009213693999. The message type may be either a message or a mini-message. One-way messages can skip a response message type.

If the name is prefixed with `*`, then it is considered a multi-message stream.


### Reserved functions.
A function's number can be reserved with:
```
reserve <function number> {, <function number>};
```

This is not needed if you want to remove a function, but it helps ensure that function numbers are unique to maintain backwards compatibility that would be broken by reusing function numbers and calling a different function than intended.


## Memory layout

A permebuf contains a metadata byte followed by the root message. The metadata byte is laid out as follows:

Bits 0-1 = The size of a pointer.
		00 = Pointers are 1 byte.
		01 = Pointers are 2 bytes.
		10 = Pointers are 4 bytes.
		11 = Pointers are 8 bytes.

### Messages

Messsages are encounded as a variable length number, indicating the size of the message (excluding the variable length number.) The fields of the message then follow, in order by their field number.

`bool`s are grouped together into groups of 8, and compressed as a bit field. `int`s and `float`s are represented inline. Enums are stored inline as 2 byte values. `any`s, `list`s, `array`s, `string`s, and messages are stored as pointers to an object.

Unset fields are initialized to 0. Attempting to read a field that is beyond a message's length is valid operation (allowing backwards and forwards compatibility), but it is assumed that the data is just 0.

Accessing a pointer who's value is 0 is valid, and is akin to as if all the data in the object is also set to 0.

### Variable length numbers

Prevariable length numbers are encoded in 1 to 9 bytes, with the first byte containing the length of the number, as follows:

xxxx,xxx0 = 7-bit number, 1 byte
xxxx,xx01 = 14-bit number, 2 bytes
xxxx,x011 = 21-bit number, 3 bytes
xxxx,0111 = 28-bit number, 4 bytes
xxx0,1111 = 35-bit number, 5 bytes
xx01,1111 = 42-bit number, 6 bytes
x011,1111 = 49-bit number, 7 bytes
0111,1111 = 56-bit number, 8 bytes
1111,1111 = 64-bit number, 9 bytes

### Any
An `any` is encoded inline as a pointer to the object.

### Oneof
A `oneof` is encoded inline as a 2 byte selector followed by a pointer to the object.

### Arrays

Arrays are stored by a variable length number, indicating how many items are on the array, followed by each entry (which may either be a pointer or an inlined type.)

### Lists

Lists are stored as pointers to the first list entry. A list entry contains a pointer to the next entry, followed by the current entry (which may either be a pointer or an inlined type.)

### Shared memory buffer

A shared memory buffer is stored by its 8-byte identifier.

### Strings

Strings are stored as a variable length number indiciating how many bytes are in the string, followed by the bytes of the string. Bytes are stored the same as strings.

## C++ API

The .permebuf files get converted into C++ header and source files at build time. You can include permebuf files from your own application as:

```
#include "permebuf/mine/<permebuf file>permebuf.h"
```

Or, from a library as:

```
#include "permebuf/<library name>/<permebuf file>.permebuf.h"
```

Permebuf messages and enums live in the `permabuf` C++ namespace, followed by the permabuf namespace.

### Permabuf

The top level class is a `Permebuf<Root Message>` (with `Root Message` being your root message.) Permebuf messages can't be copied unless explicitly via `.clone`. This is intential, to avoid accidental copy operations, as such it is advised that you wrap the `Permebuf` in a `std::unique_ptr`.

New permebuf can be created with `Permebuf<Root Message>::Create(pointer_bytes)` where pointer bytes are 1, 2, 4, or 8. This affects the max size, to which the underlying memory buffer can grow to.

`pointer_bytes` = EIGHT_BIT_POINTERS, the permebuf can't grow greater than 256 bytes.
`pointer_bytes` = SIXTEEN_BIT_POINTERS, the permebuf can't grow greater than 64 kilobytes.
`pointer_bytes` = THIRTY_TWO_BIT_POINTERS, the permebuf can't grow greater than 4 gigabytes.
`pointer_bytes` = SIXTY_FOUR_BIT_POINTERS, the permebuf can't grow greater than 16 exabytes.

A read-only const permebuf can be created with: `Permebuf<Root Message>::Read(pointer, length)` where pointer is a memory address, and length is the length of the permebuf.

Permabufs are optimized for the "write once, read many" use case. A permabuf acts as a memory arena, so allocations happen by appending memory to the end of the arena. Assigning a new value to a pointer type will allocate memory without deallocating the old object.

### Messages

Messages have the following members:

* `bool IsValid() const` - Returns if the message itself is a null-pointer. An null-pointer message is a message that doesn't really exist, because it was a wrapper around a null-pointer. All fields in this message will be 0/null.
* `int CountFields() const` - Returns the number of fields in the message.
* `FieldType GetFieldType(int field_num) const` - Returns the type of the field for field `i`.
* `std::optional<int> GetField(std::string_view field_name) const` - Returns the field number for a field with the given name.
* `std::optional<std::string_view> GetFieldName(int field_num) const` - Returns the name of this field.
* `std::optional<T> GetField<T>(int i) const` - Read the field.
* `void SetField<T>(int field_num, T value)` - Sets a field's value.
* `bool HasField(int i) const` - Returns true if a field exists and either: it's a non-pointer value, or it's a pointer value and has been allocated something.

Inline message fields (ints, floats, enums) add the following members: (`FIELD` is replaced with the field name.)
* `bool HasFIELD() const` - Returns true, because these are not pointer values.
* `T GetFIELD() const` - Returns the value of the field.
* `void SetFIELD(T value)` - Sets the value of the field.

String fields add the following members:
* `bool HasFIELD() const` - Returns true if a string is set.
* `std::string_view GetFIELD() const` - Returns the string if set, or a blank string.
* `void SetFIELD(absl::string_view string)` - Sets the string.
* `void SetFIELD(void* string, int length)` - Sets the string.
* `void SetFIELD(PermabufString string)` - Sets the string to the permabuf string (without making another instance of it).
* `void ClearFIELD()` - Clears the string (but doesn't actually deallocate memory.)

Bytes fields add the following members:
* `bool HasFIELD() const` - Return true if bytes are set.
* `PermabufBytes GetFIELD() const` - Returns the bytes.
* `void SetFIELD(void* bytes, int length)` - Sets the bytes.
* `void SetFIELD(PermabufBytes bytes)` - Sets the bytes to thepermabuf bytes (without making another instance of it).
* `void ClearFIELD()` - Clears the bytes (but doesn't actually deallocate memory.)

List fields add the following members:
* `bool HasFIELD() const` - Returns true if a list is set.
* `const PermabufList<T> GetFIELD() const` - Returns a const list.
* `PerabufList<T> MutableFIELD()` - Returns a mutable list, allocating it if it's not set.
* `void SetFIELD(PermabufList<T> list)` - Sets the list to a permabuf list (without making another instance of it).
* `void ClearFIELD()` - Clears the list (but doesn't actually deallocate memory.)

Array fields add the following members:
* `bool HasFIELD() const` - Returns true if an array is set.
* `const PermabufArray<T> GetFIELD() const` - Returns a const array.
* `PermabufArray<T> MutableFIELD()` - Returns an array, allocating an array of size 0 if it's not set.
* `PermabufArray<T> MutableFIELD(int size)` - Returns an array. If the array size is not allocated, or does have `size` elements, then a copy of the array is made that has `size` elements.
* `void SetFIELD(PermabufArray<T> array)` - Sets the array to a permabuf array (without making another instance of it).
* `void ClearFIELD()` - Clears the array (but doesn't actually deallocate memory.)

Oneof fields and message fields add the following members:
* `bool HasFIELD() const` - Returns true if the field is set.
* `const T GetFIELD() const` - Returns the field.
* `T MutableFIELD()` - Returns a mutable copy of the field. This allocates the object if it doesn't exist.
* `void SetFIELD(T value)` - Sets the field to a permabuf object (without making another instance of it).
* `void ClearFIELD()` - Clears the field (but doesn't actually deallocate memory.)

Shared memory buffers have the following members:
* `bool HasFIELD() const` - Returns true if the field is set.
* `SharedMemory GetFIELD() const` - Returns the shared memory.
* `void SetFIELD(const SharedMemory& value)` - Assigns the shared memory buffer.
* `void ClearField()` - Clears the shared memory buffer.

### List

List objects have the following members:

* `bool IsValid() const` - Returns false if we're at the end of the list.
* `int Length() const` - Counts the number of remaining items on the list.
* `const List<T> GetNext() const` - Iterates to the next item on the list.
* `List<T> MutableNext()` - Iterates to the next item on the list and allocates it if it doesn't exist.
* `void ClearNext()` - Clears the next item on the list, making this the new end of the list (but doesn't actually deallocate memory.)
* `const List<T> GetAt(i) const` - Iterates so many items up the list.
* `List<T> MutableAt(i)` - Iterates so many items up the list and allocates along the way if they don't exist.

Lists have the same methods as the field would have added to a message, however the field's name is 'Value'.

### Array

Array objects have the following members:

* `bool IsValid() const` - Returns false if the array doesn't exist or has 0 elements.
* `int Length() const` - Counts the number of items on the array.

Lists have the same methods as the field would have added to a message, however the field's name is 'Value' and the first parameter is an iterator.

### Oneof

Oneof objects have the following members:

* `bool IsValid() const` - Returns false if no value is set.
* `bool HasFIELD() const` - Returns true if the field is set.
* `const T GetFIELD() const` - Returns a field. If the field isn't set, then it returns a null-pointer message/list/array.
* `T MutableFIELD()` - Returns a mutable field. If the field isn't set, then it makes this the set field for the one of (but doesn't actually deallocate memory.)
* `void Clear()` - Unsets any set field (but doesn't actually deallocate memory.)
* `Option GetOption() const` - Gets the currently set option.

Oneofs have a nested enum called `Options`.

### Services

Services have two classes: `<ServiceName>` (the client) and `<ServiceName>::Server` (server implementations).

Service clients have the following members:
* `void Send<MethodName>(Request) const` - Sends a one-way message. If the request is a mini-message, then the C++ type is that object, otherwise, the C++ type is `std::unique_ptr<Permebuf<message type>>`, and requests must be moved to the call (because the memory holding the Permebuf is transfered to the callee.)
* `StatusOr<Response> Call<MethodName>(Request) const` - Issues an RPC to a two-way non-streaming message and waits for a response. If a type is a mini-message, then the C++ type is that object, otherwise, the C++ type is `std::unique_ptr<Permebuf<message type>>`, and requests must be moved to the call (because the memory holding the Permebuf is transfered to the callee.)
* `void Call<MethodName>(Request, const std::function<void(StatusOr<Response>)>& on_response) const` - Asynchronous issues a two-way non-streaming message. If a type is a mini-message, then the C++ type is that object, otherwise, the C++ type is `std::unique_ptr<Permebuf<message type>>`, and requests must be moved to the call (because the memory holding the Permebuf is transfered to the callee.)
* `PermebufStream Open<MethodName>(Request) const` - Opens a stream call, passing the initial request message. If the request is a mini-message, then the C++ type is that object, otherwise, the C++ type is `std::unique_ptr<Permebuf<message type>>`, and requests must be moved to the call (because the memory holding the Permebuf is transfered to the callee.)
* `ProcessId ProcessId() const` - Gets the ID of the serving process.
* `MessageId MessageId() const` - Gets the ID of the serving message.
* `void OnDestroy(const std::function<void()>&)` - Registers a function to be called when the service is destroyed. If the client object is destroyed before the service, then this function isn't ever called.
* `bool operator==(const <ServiceName>Client&) const` - Returns true if both client objects are pointing to the same service instance.

Service clients have the following static members:
* `std::optional<<ServiceName>Client> FindFirstInstance() const` - Finds the first instance of a service.
* `void ForEachInstance(const std::function<void(const <ServiceName>Client&)>&)` - Loops over each instance of the service.
* `MessageId RegisterNotificationOnEachInstance(const std::function<void(const <ServiceName>Client&)>&)` - Calls the handler each time a new instance of the service appears. This applies retroactively, as the handler is also called for every existing instance of the service.
* `void UnegisterNotificationOnEachInstance(MessageId)` - Unregisters the handler so it is not called for any more new instances of the service.

Implementations inherit an instance of `<ServiceName>::Server`, override the handlers, then create an instance of it. The interface has the following methods:
* `void <MethodName>(Request)` - Called when a one-way non-stream request occurs.
* `void <MethodName>(Request, PermabufResponse<Response> response)` - Called when a two-way non-stream request occurs.
* `void <MethodName>(Request, Stream stream)` - Called when a stream request occurs.

The PermabufResponse object has the following methods:
* `void ReplyWith(Response)` - Replies with a reponse. If the response type is a mini-message, then the C++ type is that object, otherwise, the C++ type is `std::unique_ptr<Permebuf<message type>>`, and requests must be moved to the call (because the memory holding the Permebuf is transfered to the caller.)
* `void ReplyWithStatus(StatusCode)` - Replies with a status code.
After a PermabufResponse has been replied to, no further calls should be made.

The Permabuftream object has the following methods:
* `StatusOr<Message> Read()` - Waits for a message.
* `StatusOr<Message> Write()` - Writes a message.
* `void FinalWrite(Message)` - Writes a message and closes the stream.
* `void CloseWithStatus(StatusCode)` - Closes the stream with a status code.
* `bool HasMessage()` - Did the other side send a message, and is there one waiting?
* `bool IsOpen()` - Is the stream still open?



