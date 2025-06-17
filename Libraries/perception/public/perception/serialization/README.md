# Perception Serialization

The intent of the Perception serialization framework is to serialize an object into a binary stream so that it can be sent across processes (or potentially saved to a file), then deserialized on the other side, in a way where the serialized binary is backwards and forwards compatible.

Previously, Perception depended on [Permebufs](../../../../../Build/Permebuf.md), but I wanted a pure C++ solution that didn't depend on generated code.

The goals of the serialization framework are:
* Make (de)serializing fast and compact.
* Minimize the boilerplate needed to make a class serializable.

To create a serializable class, inherit your class from [`Serilizable`](serializable.h) and override `void Serialize(Serializer &serializer)`, then pass to the [`Serializer`](serializer.h) each of the serializable fields.

Example:

```
class MyObject : public Serializable {
  public:
    std::string name;
    int age;

    virtual Serialize(Serializer& serializer) {
        serializer.String("Name", name);
        serializer.Integer("Age", age);
    }
};
```

Then to print this object out, you can serialize is to a human readable string with [`SerializeToString`](text_serializer.h):
```
std::cout << SerializeToString(object) << std::endl;
```

You can serialize to a `std::vector<std::byte>` with [`SerializeToByteVector`](vector_write_stream.h), to shared memory with [`SerializedToSharedMemory`](shared_memory_write_stream.h), etc. You can deserialize with [`DeserializeFromByteVector`](memory_read_string.h), [`DeserializeFromSharedMemory`](memory_read_string.h), etc. There's even a [`DeserializeToEmpty`](memory_read_string.h) to reset all the values back to null/0/empty.

The string name passed to serializer for each field does not influence the size of anything when serialized to binary. This name is only used when serializing to a string (e.g. to print out the value of the serializable).

In order to remain backwards/forward compatible, the method calls to the serializer are order dependent. If you no longer care about a field, you can skip it by providing no parameters, e.g. `serializer.String()` will skip a string (either passing it on deserialization, or not outputting anything and saving memory on serialization). Don't change the types, as the serializer needs to know the type of value to skip it.

You can serialize fields that themselves are serializble with `serializer.Serializable(...)`. You can also pass this a `std::shared_ptr<Serializable>` and it will only serialize if the `shared_ptr` isn't empty (and set the `shared_ptr` if the field is found).

You can serialize an `std::vector<Serializable>` or `std::vector<stdd:shared_ptr<Serilizable>>` with `serializer.ArrayOfSerializables(...)`.

Do not put anything behind loops that can run a dynamic number of times. Conditions are risky - make sure each field is serialized or kipped on every run.

You can conditionally branch (e.g. to implement a one-of statement), as long as the number of serialization steps stays consistent and they are of the same type (so you can mix and match `Serializable` objects), and handle the unknown/default case, e.g.:

```
void MyObject::Serialize(Serializer& serializer) {
    serializer.Integer("Fruit Type", fruit_type_);
    switch(type_) {
        case APPLE:
            serializer.Serializable("Apple", apple_);
            break;
        case BANANA:
            serializer.Serializable("Banana", banana_);
        default:
             serializer.Serializable();
             break;
   }
}
```
