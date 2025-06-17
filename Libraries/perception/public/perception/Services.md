# Perception Services Framework
The services framework provide a way to call a method from one process, and have the method run in another process, with the parameters and responses serialized and deserialized automatically.

Previously, Perception depended on [Permebufs](../../../../../Build/Permebuf.md), but I wanted a pure C++ solution that didn't depend on generated code.

The goals of the services framework are:
* Make calling and implementing services feel as natural as possible as C++ method calls.
* Minimize the boilerplate code.
* Have a concrete, typesafe API that defines all the methods on a service.

Defining a service requires the use of the [`DEFINE_PERCEPTION_SERVICE`](service_macros.h) macro. Here's an example.

Let's specify some serializable types for the request and responses:
```
using ::perception::serialization::Serializable;
using ::perception::serialization::Serializer;

class SingleValue : public Serializable {
 public:
  float value;

  SingleValue() {}
  SingleValue(float value) : value(value) {}

  virtual void Serialize(Serializer& serializer) override {
    serializer.Float("Value", value);
  };
};

class DoubleValue : public Serializable {
 public:
  float a;
  float b;

  DoubleValue() {}::pe
  DoubleValue(float a, float b) : a(a), b(b) {}

  virtual void Serialize(Serializer& serializer) override {
    serializer.Float("A", a);
    serializer.Float("B", b);
  };
};
```

Defining the service:
```
#define METHOD_LIST(X)          \
  X(1, Add, SingleValue, DoubleValue)      \
  X(2, Subtract, SingleValue, DoubleValue) \
  X(3, Negate, SingleValue, SingleValue)   \
  X(4, GetSavedValue, SingleValue, void)      \
  X(5, SetSavedValue, void, SingleValue)

DEFINE_PERCEPTION_SERVICE(Calculator, "perception.Calculator", METHOD_LIST)
#undef METHOD_LIST
```

To define the service, use:
```
DEFINE_PERCEPTION_SERVICE(class_name, fully_qualified_name, method_list)
```

It takes 3 parameters:
 * `class_name` - The generated class's name in C++.
 * `fully_qualified_name` - The fully qualified name of the service. This must be unique system wide, as it's used to uniquely identify this service.
 * `method_list` - An X macro containing the method list.

The MethodList X macro takes a single X parameter, and calls it for each RPC method:
```
X(id, method_name, return_type, argument_type)
```

The parameters are:
* `id` - The unique ID of this method. Methods do not have to be in sequential order, and unhandled methods can be skipped, but for background compatibility, recycling IDs has undefined behavior.
* `method_name` - The name of the method.
* `return_type` - The return type. Must either be a Serializable or void.
* `*argument_type` - The argument type. Must either be a Serializable or void.

It is safe to add and remove methods (another process calling a non-existent method will return a bad status), but recycling IDs is unsafe if you care about backwards compatibility.

This automatically generates a class:
```
class Calculator {
  public:
    virtual StatusOr<SingleValue> Add(const DoubleValue& input);
    virtual StatusOr<SingleValue> Subtract(const DoubleValue& input);
    virtual StatusOr<SingleValue> Negate(const SingleValue& input);
    virtual StatusOr<SingleValue> GetSavedValue();
    virtual Status SetSavedValue(const SingleValue& input);
};
```

It will also generate 2 nested classes `Calculator::Server` and `Calculator::Client`.

To implement a server, subclass `Calculactor::Server` and overide the methods. Here's a complete implementation of `Calculator`:

```
class CalculatorImpl : public Calculator::Server {
 public:
  CalculatorImpl() : saved_value_(0) {}

  virtual ~CalculatorImpl() {}

  virtual StatusOr<SingleValue> Add(DoubleValue& inputs) override {
    return SingleValue(inputs.a + inputs.b);
  }

  virtual StatusOr<SingleValue> Subtract(DoubleValue& inputs) override {
    return SingleValue(inputs.a - inputs.b);
  }

  virtual StatusOr<SingleValue> Negate(SingleValue& input) override {
    return SingleValue(-input.value);
  }

  virtual StatusOr<SingleValue> GetSavedValue() override { return SingleValue(saved_value_); }

  virtual perception::Status SetSavedValue(SingleValue& input) override {
    saved_value_ = input.value;
    return perception::Status::OK;
  }

 private:
  float saved_value_;
};
```

If you care about knowing who the caller is, you can also override a variant instead that takes the caller's `ProcessId`:
```
virtual StatusOr<SingleValue> Add(DoubleValue& inputs, ProcessId caller) override;
```

Then, to create an instance of the service, just create an instance of the object and keep it in memory. It'll get registered with the kernel and become discorable via other processes. e.g.:

```
int main(int argc, char* argv[]) {
    auto calculator = std::make_unique<CalculatorImpl>();

    // Hands control over to the event loop without quiting.
    HandOverControl();
    return 0;
}
```

There are a set of methods in [services.h](services.h) for finding services such as `NotifyOnEachNewServiceInstance` which will call a function each time a new instance of that service appears:

```
NotifyOnEachNewServiceInstance<Calculator>(
    [](Calculator::Client calculator) {
    // New calculator instance appeared.
})
```

There's also `FindFirstInstanceOfService` will return a `std::optional<>` either containing the service if it's running, or an empty optional if it's not running. There's also a `GetService` function that will find the first instance of a service, but if none are running, will block and wait for one.

```
auto calculator = GetService<Calculator>();

std::cout << "2+5 is.." << std::flush;
auto response = calculator.Add(DoubleValue(2.0f, 5.0f));
if (!response.IsOk()) return; // Error
std::cout << response->value << std::endl;
```

Prefer to use the status macros in [status.h](../status.h) to gracefully propogate errors around, e.g.

```
StatusOr<float> AddUsingRemoteCalculator(float a, float b) {
    ASSIGN_OR_RETURN(auto response, calculator.Add(DoubleValue(a, b)));
    return response.value;
}
```

The generated methods have an asynchronous version that takes an optional callback method:
```
calculator.Add(DoubleValue(a,b),
    [](const StatusOr<SingleValue>& response) {
    // Handle response.
});
```

The callback method is optional. If you just want to send a one way message to a service and don't care about the response or status, you can pass an empty callback. This is slightly more efficient as there's no message sent back from the callee to the caller:

```
// Synchronously calls the server, blocking until it has responded.
auto status = calculator.SetSavedValue(SingleValue(10.0f));

// Asynchronously calls the server, calling the callback when the server
// has responded.
calculator.SetSavedValue(SingleValue(10.0f), [](const Status& status) {

});

// Asynchronously calls the server, telling the server there's no need to
// send a response.
calculator.SetSavedValue(SingleValue(10.0f), {});
```
