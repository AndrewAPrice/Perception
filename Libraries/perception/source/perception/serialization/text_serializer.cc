

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
#include <types.h>

#include <string>

#include "perception/serialization/serializable.h"
#include "perception/serialization/serializer.h"

namespace perception {
namespace serialization {

namespace {
class TextSerializer : public Serializer {
 public:
  TextSerializer(int indentation, std::string* output_string)
      : indentation_(indentation), output_string_(output_string) {
    output_string_->append("{\n");
  }

  virtual bool HasThisField() override {
    // Not needed for serializing.
    return false;
  }

  virtual bool IsDeserializing() override {
    // Only serializing is supported.
    return false;
  }

  virtual void Integer() override {}

  virtual void Integer(std::string_view name, uint8& value) override {
    auto value64 = static_cast<uint64>(value);
    Integer(name, value64);
  }

  virtual void Integer(std::string_view name, int8& value) override {
    auto value64 = static_cast<int64>(value);
    Integer(name, value64);
  }

  virtual void Integer(std::string_view name, uint16& value) override {
    auto value64 = static_cast<uint64>(value);
    Integer(name, value64);
  }

  virtual void Integer(std::string_view name, int16& value) override {
    auto value64 = static_cast<int64>(value);
    Integer(name, value64);
  }

  virtual void Integer(std::string_view name, uint32& value) override {
    auto value64 = static_cast<uint64>(value);
    Integer(name, value64);
  }

  virtual void Integer(std::string_view name, int32& value) override {
    auto value64 = static_cast<int64>(value);
    Integer(name, value64);
  }

  virtual void Integer(std::string_view name, uint64& value) override {
    AppendIndentation();
    output_string_->append(name);
    output_string_->append(": ");
    output_string_->append(std::to_string(value));
    output_string_->append("\n");
  }

  virtual void Integer(std::string_view name, int64& value) override {
    AppendIndentation();
    output_string_->append(name);
    output_string_->append(": ");
    output_string_->append(std::to_string(value));
    output_string_->append("\n");
  }

  virtual void Float() override {}

  virtual void Float(std::string_view name, float& value) override {
    auto double_value = static_cast<double>(value);
    Double(name, double_value);
  }

  virtual void Double() override {}

  virtual void Double(std::string_view name, double& value) override {
    AppendIndentation();
    output_string_->append(name);
    output_string_->append(": ");
    output_string_->append(std::to_string(value));
    output_string_->append("\n");
  }

  virtual void String() override {}

  virtual void String(std::string_view name, std::string& str) override {
    AppendIndentation();
    output_string_->append(name);
    output_string_->append(": \"");
    output_string_->append(str);
    output_string_->append("\"\n");
  }

  virtual void Serializable() override {}

  virtual void Serializable(std::string_view name,
                            class Serializable& obj) override {
    AppendIndentation();
    output_string_->append(name);
    output_string_->append(": ");

    TextSerializer child_serializer(indentation_ + 2, output_string_);
    obj.Serialize(child_serializer);

    AppendIndentation();
    output_string_->append("}\n");
  }

  virtual void ArrayOfSerializables() override {}

  virtual void ArrayOfSerializables(
      std::string_view name, int current_size,
      const std::function<
          void(const std::function<void(class Serializable& serializable)>&
                   serialize_entry)>& serialization_function,
      const std::function<
          void(int elements,
               const std::function<void(class Serializable& serializable)>&
                   deserialize_entry)>& deserialization_function) override {
    if (current_size == 0) return;
    AppendIndentation();
    output_string_->append(name);
    output_string_->append(": [\n");

    std::string* output_string = output_string_;
    int child_indentation = indentation_ + 2;

    serialization_function(
        [output_string, child_indentation](class Serializable& serializable) {
          for (int i = 0; i < child_indentation; ++i)
            output_string->append(" ");

          TextSerializer child_serializer(child_indentation + 2, output_string);
          serializable.Serialize(child_serializer);

          for (int i = 0; i < child_indentation; ++i)
            output_string->append(" ");
          output_string->append("}\n");
        });

    AppendIndentation();
    output_string_->append("]\n");
  }

 private:
  void AppendIndentation() {
    for (int i = 0; i < indentation_; ++i) output_string_->append(" ");
  }

  int indentation_;
  std::string* output_string_;
};

}  // namespace

std::string SerializeToString(class Serializable& object) {
  std::string output_string = "";

  TextSerializer serializer(2, &output_string);
  object.Serialize(serializer);

  output_string.append("}");

  return output_string;
}

}  // namespace serialization
}  // namespace perception