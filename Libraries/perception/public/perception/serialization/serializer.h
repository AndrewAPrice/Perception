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

#include <types.h>

#include <functional>
#include <memory>

namespace perception {
namespace serialization {

class Serializable;

// Interface for a serializer passed to serializable.
//
// The serializer has a set of function for either serializing or skipping
// fields in a backwards-compatible way.
//
// Example usage:
//   void MyObject::Serialize(serialization::Serializer& serializer) {
//     serializer.String("Name", name_);
//     serializer.Integer();
//     serializer.Float("Size", size_);
//   }
//
// Do not put anything behind loops that can run a dynamic number of times.
// Conditions are risky - make sure each field is serialized or kipped on every
// run. The call order matters - so if you want the serializable class to remain
// backwards compatible, never shuffle or remove calls. If you no longer want to
// serialize a field, replace the call with the parameter-lesss variant, so it
// knows to skip a field and the size of the field to skip.
//
// You can conditionally branch (e.g. to implement a one-of statement), as long
// as the number of serialization steps stays consistent. e.g.:
//   void MyObject::Serialize(serialization::Serializer& serializer) {
//     serializer.Integer("Fruit Type", fruit_type_);
//     switch(type_) {
//       case APPLE:
//          serializer.Serializable(apple_);
//          break;
//       case BANANA:
//          serializer.Serializable(banana_0);
//          break;
//       default:
//          serializer.Serializable();
//          break;
//     }
//   }
class Serializer {
 public:
  // Returns whether the data source has the next field, when deserializing.
  virtual bool HasThisField() = 0;

  // Return true if deserialzing.
  virtual bool IsDeserializing() = 0;

  // Serializes a field. The parameterless variations skip over the field.
  virtual void Integer() = 0;
  template <class T>
  void Integer(std::string_view name, T& value) {
    if constexpr (std::is_signed_v<T>) {
      if (IsDeserializing()) {
        int64 v = 0;
        SignedInteger(name, v);
        value = static_cast<T>(v);
      } else {
        int64 v = static_cast<int64>(value);
        SignedInteger(name, v);
      }
    } else {
      if (IsDeserializing()) {
        uint64 v = 0;
        UnsignedInteger(name, v);
        value = static_cast<T>(v);
      } else {
        uint64 v = static_cast<uint64>(value);
        UnsignedInteger(name, v);
      }
    }
  }

  virtual void Float() = 0;
  virtual void Float(std::string_view name, float& value) = 0;

  virtual void Double() = 0;
  virtual void Double(std::string_view name, double& value) = 0;

  virtual void String() = 0;
  virtual void String(std::string_view name, std::string& str) = 0;

  virtual void Serializable() = 0;
  virtual void Serializable(std::string_view name, class Serializable& obj) = 0;

  template <class S>
  void Serializable(std::string_view name, std::shared_ptr<S>& obj) {
    if (IsDeserializing()) {
      if (HasThisField()) {
        if (!obj) obj = std::make_shared<S>();
        this->Serializable(name, *obj);
      } else {
        obj.reset();
        this->Serializable();
      }
    } else {
      if (obj) {
        this->Serializable(name, *obj);
      } else {
        this->Serializable();
      }
    }
  }

  virtual void ArrayOfSerializables() = 0;

  virtual void ArrayOfSerializables(
      std::string_view name, int current_size,
      const std::function<
          void(const std::function<void(class Serializable& serializable)>&
                   serialize_entry)>& serialization_function,
      const std::function<
          void(int elements,
               const std::function<void(class Serializable& serializable)>&
                   deserialize_entry)>& deserialization_function) = 0;

  template <class S>
  void ArrayOfSerializables(std::string_view name,
                            std::vector<std::shared_ptr<S>>& arr) {
    ArrayOfSerializables(
        name, arr.size(),
        /*serilization_function=*/
        [&arr](const std::function<void(class Serializable & serializable)>&
                   serialize_entry) {
          for (auto& entry_ptr : arr) {
            if (entry_ptr) {
              serialize_entry(*entry_ptr);
            } else {
              S empty_object{};
              serialize_entry(empty_object);
            }
          }
        },
        /*deserialization_function=*/
        [&arr](int elements,
               const std::function<void(class Serializable & serializable)>&
                   deserialize_entry) {
          arr.resize(elements);
          for (int i = 0; i < elements; i++) {
            auto& ptr = arr[i];
            if (!ptr) ptr = std::make_shared<S>();
            deserialize_entry(*ptr);
          }
        });
  }

  template <class S>
  void ArrayOfSerializables(std::string_view name, std::vector<S>& arr) {
    ArrayOfSerializables(
        name, arr.size(),
        /*serilization_function=*/
        [&arr](const std::function<void(class Serializable & serializable)>&
                   serialize_entry) {
          for (auto& entry : arr) serialize_entry(entry);
        },
        /*deserialization_function=*/
        [&arr](int elements,
               const std::function<void(class Serializable & serializable)>&
                   deserialize_entry) {
          arr.resize(elements);
          for (int i = 0; i < elements; i++) deserialize_entry(arr[i]);
        });
  }

 private:
  virtual void UnsignedInteger(std::string_view name, uint64& value) = 0;
  virtual void SignedInteger(std::string_view name, int64& value) = 0;
};

}  // namespace serialization
}  // namespace perception