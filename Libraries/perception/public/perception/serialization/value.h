// Copyright 2026 Google LLC
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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "perception/serialization/serializable.h"
#include "perception/serialization/serializer.h"
#include "status.h"

namespace perception {
namespace serialization {

// Represents a generic, strongly-typed value (e.g. for configuration, settings
// or IPC). To convert this Value to/from a compact, human-readable simplified
// JSON string representation (without keys), use the functions ValueToString()
// and StringToValue() defined in value_serialization.h.
class Value : public Serializable {
 public:
  enum class Type { UNDEFINED, BOOLEAN, INTEGER, FLOAT, STRING, ARRAY };

  Value();
  Value(bool val);
  Value(int64 val);
  Value(int val);
  Value(double val);
  Value(float val);
  Value(std::string_view val);
  Value(const char* val);
  Value(std::string val);
  Value(std::vector<Value> val);

  // Type query
  Type GetType() const;

  // Accessors
  std::optional<bool> BoolValue() const;
  std::optional<int64> IntegerValue() const;
  std::optional<double> FloatValue() const;
  std::optional<std::string_view> StringValue() const;
  const std::vector<Value>* ArrayValue() const;

  // Setters
  void SetBool(bool val);
  void SetInteger(int64 val);
  void SetFloat(double val);
  void SetString(std::string_view val);
  void SetArray(std::vector<Value> val);
  void SetUndefined();

  virtual void Serialize(Serializer& serializer) override;

 private:
  Type type_;
  bool bool_value_;
  int64 int_value_;
  double float_value_;
  std::string string_value_;
  std::vector<Value> array_value_;
};

}  // namespace serialization
}  // namespace perception
