// Copyright 2025 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <concepts>
#include <functional>

#include "perception/scheduler.h"
#include "perception/serialization/serializable.h"
#include "perception/service_client.h"
#include "perception/service_server.h"

// The following macros are used for defining services and stubbing them out.
// To define a service, use:
//   DEFINE_PERCEPTION_SERVICE(class_name, fully_qualified_name, method_list)
// It takes 3 parameters:
//   class_name - The generated class's name in C++.
//   fully_qualified_name - The fully qualified name of the service. This must
//     be unique system wide, as it's used to uniquely identify this service.
//   method_list - An X macro containing the method list.
//
// The MethodList X macro takes a single X parameter, and calls it for each
// RPC method:
//   X(id, method_name, return_type, argument_type)
// The parameters are:
//   id - The unique ID of this method. Methods do not have to be in sequential
//     order, and unhandled methods can be skipped, but for background
//     compatibility, recycling IDs has undefined behavior.
//   method_name - The name of the method.
//   return_type - The return type. Must either be a Serializable or void.
//   argument_type - The argument type. Must either be a Serializable or void.
//
// For example:
//   #define CALCULATOR_METHOD_LIST(X)          \
//     X(1, Add, SingleValue, DoubleValue)      \
//     X(2, Subtract, SingleValue, DoubleValue) \
//     X(3, Negate, SingleValue, SingleValue)
//
//   DEFINE_PERCEPTION_SERVICE(Calculator, "perception.Calculator", \
//                             CALCULATOR_METHOD_LIST)
//
// To implement the service, you can subclass the generated
// Calculator::Server and override the afformentioned methods:
//   class MyCalculator : public Calculator::Server {
//   public:
//      virtual SingleValue Add( DoubleValue& inputs) override {
//          return SingleValue(inputs.a + inputs.b);
//      }
//
//      virtual SingleValue Subtract( DoubleValue& inputs) override {
//          return SingleValue(inputs.a + inputs.b);
//      }
//
//      virtual SingleValue Negative( SingleValue& inputs) override {
//          return SingleValue(-input.value);
//      }
//   }

//////////////////
/// Helper macros.
//////////////////

// Returns 1 for one argument, and 2 for two or more arguments.
#define _GET_NTH_ARG(_1, _2, N, ...) N
#define COUNT_ARGS(...) _GET_NTH_ARG(__VA_ARGS__, 2, 1)

// Concatination macros.
#define CAT_HELPER(a, b) a##b
#define CAT(a, b) CAT_HELPER(a, b)

// IS_VOID_CHECK returns 1 for void, 2 for not void.
#define PROBE_void ~, 1
#define IS_VOID_CHECK(...) COUNT_ARGS(CAT(PROBE_, __VA_ARGS__))

#define IS_VOID_RESULT_1 0  // A result of 1 means it was NOT void.
#define IS_VOID_RESULT_2 1  // A result of 2 means it WAS void.

// Returns 0 if not void, returns 1 if void.
#define IS_VOID(argument_type) \
  CAT(IS_VOID_RESULT_, IS_VOID_CHECK(argument_type))

#define PARAM_FORMAT_0(T) const T&
#define PARAM_FORMAT_1(T)

#define NAMED_PARAM_FORMAT_0(T) const T& input
#define NAMED_PARAM_FORMAT_1(T)

#define NAMED_PARAM_WITH_COMMA_FORMAT_0(T) const T &input,
#define NAMED_PARAM_WITH_COMMA_FORMAT_1(T)

#define ARGUMENT_FORMAT_0 input
#define ARGUMENT_FORMAT_1

#define ARGUMENT_WITH_COMMA_FORMAT_0 input,
#define ARGUMENT_WITH_COMMA_FORMAT_1

#define RESPONSE_FORMAT_0(T) StatusOr<T>
#define RESPONSE_FORMAT_1(T) ::perception::Status

/////////////
/// X macros.
/////////////

#define DECLARE_SERVICE_VIRTUAL_METHOD(id, method_name, return_type,           \
                                       argument_type)                          \
  virtual CAT(RESPONSE_FORMAT_, IS_VOID(return_type))(return_type)             \
      method_name(CAT(PARAM_FORMAT_, IS_VOID(argument_type))(argument_type)) { \
    return ::perception::Status::UNIMPLEMENTED;                                \
  }

#define DECLARE_SERVICE_METHOD_ID(id, method_name, return_type, argument_type) \
  k##method_name = id,

#define METHOD_NAME_SWITCH_CASES(id, method_name, return_type, argument_type) \
  case id:                                                                    \
    return #method_name;

#define IMPLEMENT_SERVICE_CLIENT_STUB(id, method_name, return_type,         \
                                      argument_type)                        \
  virtual CAT(RESPONSE_FORMAT_, IS_VOID(return_type))(return_type)          \
      method_name(CAT(NAMED_PARAM_FORMAT_,                                  \
                      IS_VOID(argument_type))(argument_type)) override {    \
    return SyncDispatch<CAT(RESPONSE_FORMAT_,                               \
                            IS_VOID(return_type))(return_type),             \
                        argument_type>(                                     \
        CAT(ARGUMENT_WITH_COMMA_FORMAT_, IS_VOID(argument_type)) id);       \
  }                                                                         \
                                                                            \
  void method_name(                                                         \
      CAT(NAMED_PARAM_WITH_COMMA_FORMAT_,                                   \
          IS_VOID(argument_type))(argument_type)                            \
          std::function<void(                                               \
              CAT(RESPONSE_FORMAT_, IS_VOID(return_type))(return_type))>    \
              on_response) {                                                \
    AsyncDispatch<CAT(RESPONSE_FORMAT_, IS_VOID(return_type))(return_type), \
                  argument_type>(                                           \
        CAT(ARGUMENT_WITH_COMMA_FORMAT_, IS_VOID(argument_type)) id,        \
        on_response);                                                       \
  }

#define HANDLE_SERVICE_SERVER_CASE(id, method_name, return_type,  \
                                   argument_type)                 \
  case id: {                                                      \
    HandleExpectedRequest<CAT(RESPONSE_FORMAT_,                   \
                              IS_VOID(return_type))(return_type), \
                          argument_type, ServiceType>(            \
        this, &ServiceType::method_name, sender, message_data);   \
    break;                                                        \
  }

#define IMPLEMENT_SERVER_STUB(id, method_name, return_type, argument_type)     \
  virtual CAT(RESPONSE_FORMAT_, IS_VOID(return_type))(return_type)             \
      method_name(CAT(NAMED_PARAM_WITH_COMMA_FORMAT_, IS_VOID(argument_type))( \
          argument_type) ProcessId sender) {                                   \
    return method_name(CAT(ARGUMENT_FORMAT_, IS_VOID(argument_type)));         \
  }                                                                            \
                                                                               \
  virtual CAT(RESPONSE_FORMAT_, IS_VOID(return_type))(return_type)             \
      method_name(CAT(NAMED_PARAM_FORMAT_,                                     \
                      IS_VOID(argument_type))(argument_type)) override {       \
    return ::perception::Status::UNIMPLEMENTED;                                \
  }

//////////////////////////////////
/// Macro for declaring a service.
//////////////////////////////////
#define DEFINE_PERCEPTION_SERVICE(class_name, fully_qualified_name,       \
                                  method_list)                            \
  class class_name##_Server;                                              \
  class class_name##_Client;                                              \
                                                                          \
  class class_name {                                                      \
   public:                                                                \
    typedef class_name##_Server Server;                                   \
    typedef class_name##_Client Client;                                   \
    virtual ~class_name() = default;                                      \
                                                                          \
    enum MethodIds { method_list(DECLARE_SERVICE_METHOD_ID) };            \
                                                                          \
    static std::string_view MethodName(int method_id) {                   \
      switch (method_id) {                                                \
        default:                                                          \
          return "Unknown";                                               \
          method_list(METHOD_NAME_SWITCH_CASES)                           \
      }                                                                   \
    }                                                                     \
                                                                          \
    static std::string_view FullyQualifiedName() {                        \
      return fully_qualified_name;                                        \
    }                                                                     \
                                                                          \
    method_list(DECLARE_SERVICE_VIRTUAL_METHOD)                           \
  };                                                                      \
                                                                          \
  class class_name##_Server : public class_name,                          \
                              public ::perception::ServiceServer {        \
   protected:                                                             \
    class_name##_Server(::perception::ServiceServerOptions options = {})  \
        : ServiceServer(options, class_name::FullyQualifiedName()) {}     \
                                                                          \
    virtual ~class_name##_Server() {}                                     \
                                                                          \
   method_list(IMPLEMENT_SERVER_STUB)                                     \
                                                                          \
       private :                                                          \
                                                                          \
       void HandleRequest(                                                \
           const ::perception::ProcessId sender,                          \
           const ::perception::MessageData& message_data) override {      \
      using ServiceType = std::decay_t<decltype(*this)>;                  \
      switch (message_data.param1) {                                      \
        default: /* Handle unknown method error */                        \
          HandleUnknownRequest(sender, message_data);                     \
          break;                                                          \
          /* Use the list a final time to generate the dispatch table. */ \
          method_list(HANDLE_SERVICE_SERVER_CASE)                         \
      }                                                                   \
    }                                                                     \
  };                                                                      \
                                                                          \
  class class_name##_Client : public class_name,                          \
                              public ::perception::ServiceClient {        \
   public:                                                                \
    class_name##_Client() : ServiceClient(0, 0) {}                        \
                                                                          \
    class_name##_Client(::perception::ProcessId process_id,               \
                        ::perception::MessageId message_id)               \
        : ServiceClient(process_id, message_id) {}                        \
                                                                          \
    class_name##_Client(const class_name##_Client& other)                 \
        : ServiceClient(other.process_id_, other.message_id_) {}          \
                                                                          \
    class_name##_Client(const class_name##_Server& other)                 \
        : ServiceClient(other.ServerProcessId(), other.ServiceId()) {}    \
                                                                          \
    virtual ~class_name##_Client() {}                                     \
                                                                          \
    method_list(IMPLEMENT_SERVICE_CLIENT_STUB)                            \
  };
