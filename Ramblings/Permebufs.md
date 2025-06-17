[Permebuf](https://github.com/AndrewAPrice/Perception/blob/master/Build/Permebuf.md) (Perception Memory Buffers) is the [interface description language](https://en.wikipedia.org/wiki/Interface_description_language) I built for Perception. [Here are some examples](https://github.com/AndrewAPrice/Perception/tree/master/Libraries/perception/permebuf) to get a feel for the language.

NOTE: Permebufs have been deprecated and replaced by the [serialization](../Libraries/perception/public/perception/serialization/README.md) and [services](../Libraries/perception/public/perception/Services.md) frameworks.

An operating system built around a microkernel involves making many calls between
applications. There is code that lives in processes that we want other processes to
be able to discover and call. It's important that we can define a common interface that multiple processes can use. For example,
all disk drivers should have a function to read data from it, and another function to get the size of the disk, regardless
If the process is actually operates on an IDE hard disk, USB drive, or Blu-ray disc.

I was inspired by interface description languages such as [Flatbuffers](https://google.github.io/flatbuffers/) and [Protocol Buffers](https://developers.google.com/protocol-buffers). They allow the programmer to define messages in their IDL and generate nice classes in the language of choice for accessing and manipulating those messges. By defining our response and request types of
our remote procedure calls in an IDL, we can make sure that regardless of the language of the caller and callee, we have share a common, well-defined
data type, and it's documented in the IDL what the possible fields are. I'm also inspired by [gRPC](https://grpc.io/) and [Cap'n Proto](https://capnproto.org/) for how they define services (a collection of
RPCs) in their IDL.

With Permebufs I wanted to make them as lightweight as possible, because a microkernel environment sends a lot of messages around. For example, every mouse movement, every draw
operation, every disk read, etc. is a message. One the few reasons I wanted to build my own IDL (besides it's fun) is because I wanted zero serialization. Protocol Buffers, for example, require you to populate the objects, then serialize into a byte stream to send. The receivers then has to deserialize it back into objects.
I wanted to avoid this. I want my getter and setters to simply point and directly operate on the byte stream, so it can be sent as-is.

To make interprocess communication fast, we need to avoid copying data. We should build variable sized messages and "gift" the memory pages they're on to the receiver. I wanted to make it explicit in C++ that you're moving the Permebuf. For example, this is what it looks like to send a message to another process:

```
Permebuf<CustomRequestType> request;
request->SetSomeValue(1234);
StatusOr<Permebuf<CustomResponseType>> response = myService->CallSomeFunction(std::move(request));
```

The `Permebuf<>` is a container that ensures all of the data inside lives in page-aligned memory. This container can also grow dynamically by grabbing additional memory pages when needed.
When we pass the Permebuf to an RPC, the `std::move(request)` makes it explicit that the data is being moved, consistent with how you move a `std::unique_ptr`.

Permebufs are intended to be used for RPCs and not storage, so they're designed to be efficient for write-once data. The `Permebuf<>` container knows the size of all the data allocated in it, so allocating more memory (be it for strings, submessages, arrays, etc - which must all live in the same `Permebuf<>` container as the root message) simply allocates it to the end of the Permebuf and increments a pointer.
Since this doesn't offer a mechanism to free memory (other than freeing the entire `Permebuf<>`) if you, say, overwrite a string field, both the new and old strings will continue to take up space in the `Permebuf<>` container.

While designing this, I realized that a lot of messages are small and fixed size. For example, to notify a program that the mouse moved, we only need to pass two 16-bit integers representing the mouse coordinates.
To read part of an open file, all you need is the start position, length to read, and the buffer to read into. Would it be possible to fit these small messages completely inside if registers and not have to deal with moving memory pages?
The system calls for sending and receiving data require many parameters (the syscall number, the caller/callee, the service to send to, the RPC being called, flags, offset and length of the data being sent) but I had two unused general purpose registers, and I could repurpose two more (the offset and length parameters), giving me a total of four 64-bit registers, or 32 bytes to try to pass register-only messages.
This lead to the invention of the `minimessage`. Minimessages are defined with the same syntax as messages but with a few restrictions: they need to be fixed size (so no arrays, strings, submessages, etc) and they need to be 32 bytes of smaller, which we can easily verify at build time. Because minimessages are so small, we can pass them around by value, and they don't need to be wrapped in a `Permebuf` container.

The request and response types of RPCs can be either a `message` or `minimessage`. RPCs can also omit a response type of you want your message to be one way. For example, if you want to notify a window that it gained focus, the caller will want to send-and-forget and not be blocked waiting for a response.

RPCs are grouped together into services. Defining a Permebuf service generates two C++ objects: a stub for clients to call, and a server for services to implement.
Services can choose to be registered with the kernel via their fully qualified name (e.g. `perception.devices.GraphicsDevice`), hopefully we can avoid name collisions.
There are system calls to query the registered services, get notified when a new service is registered (the virtual file system is listening for when a new `StorageDevice` appears), etc.
These system calls are all wrapped in the generated C++ stub and server classes.
One very useful function is the static `Get()` on the stub, making it possible to do things like: `StorageManager::Get()->OpenFile(request)` and `Get()` will find the first registered `StorageManager` or block if it doesn't exist until it does.
(Can anyone say they're a `StorageManager`? I'll cover that in a different rambling.)
All of the generated functions have synchronous and asynchronous versions.

To implement a server, you just need to inherit from the generated server object and implement the `StatusOr<ResponseType> HandleSomeMethod(ProcessId sender, RequestType request)` for each method you want to handle.
Create an instance of your class and it can be called from other processes.

You can pass around services as if it were any other datatype in messages and minimessages. For example, the `OpenFileResponse` minimessage that you receive from calling `OpenFile` on the `StorageManager` has a `File` field.
`File` is actually a service, with RPCs such as `Read` and `Close`.
The file system driver creates a new object that inherits from `File::Server`, implements `HandleRead` and `HandleClose`, then another process can call these RPCs directly on this new `File` service.
(In this case, we don't want to register this `File` service with the kernel unless we want it possible for a random process to be able to query this `File`.)
This object oriented approach allows us to write cleaner interfaces, smaller messages (e.g. we don't have to say what file we're operating on for each read), and we can also pass services between processes.
