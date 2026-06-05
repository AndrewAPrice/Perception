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

#include "perception/serialization/value.h"

namespace perception {
namespace serialization {

Value::Value()
    : type_(Type::UNDEFINED),
      bool_value_(false),
      int_value_(0),
      float_value_(0.0) {}

Value::Value(bool val)
    : type_(Type::BOOLEAN),
      bool_value_(val),
      int_value_(0),
      float_value_(0.0) {}

Value::Value(int64 val)
    : type_(Type::INTEGER),
      bool_value_(false),
      int_value_(val),
      float_value_(0.0) {}

Value::Value(int val)
    : type_(Type::INTEGER),
      bool_value_(false),
      int_value_(val),
      float_value_(0.0) {}

Value::Value(double val)
    : type_(Type::FLOAT),
      bool_value_(false),
      int_value_(0),
      float_value_(val) {}

Value::Value(float val)
    : type_(Type::FLOAT),
      bool_value_(false),
      int_value_(0),
      float_value_(val) {}

Value::Value(std::string_view val)
    : type_(Type::STRING),
      bool_value_(false),
      int_value_(0),
      float_value_(0.0),
      string_value_(val) {}

Value::Value(const char* val)
    : type_(Type::STRING),
      bool_value_(false),
      int_value_(0),
      float_value_(0.0),
      string_value_(val) {}

Value::Value(std::string val)
    : type_(Type::STRING),
      bool_value_(false),
      int_value_(0),
      float_value_(0.0),
      string_value_(std::move(val)) {}

Value::Value(std::vector<Value> val)
    : type_(Type::ARRAY),
      bool_value_(false),
      int_value_(0),
      float_value_(0.0),
      array_value_(std::move(val)) {}

Value::Type Value::GetType() const { return type_; }

std::optional<bool> Value::BoolValue() const {
  if (type_ == Type::BOOLEAN) return bool_value_;
  return std::nullopt;
}

std::optional<int64> Value::IntegerValue() const {
  if (type_ == Type::INTEGER) return int_value_;
  return std::nullopt;
}

std::optional<double> Value::FloatValue() const {
  if (type_ == Type::FLOAT) return float_value_;
  return std::nullopt;
}

std::optional<std::string_view> Value::StringValue() const {
  if (type_ == Type::STRING) return string_value_;
  return std::nullopt;
}

const std::vector<Value>* Value::ArrayValue() const {
  if (type_ == Type::ARRAY) return &array_value_;
  return nullptr;
}

void Value::SetBool(bool val) {
  type_ = Type::BOOLEAN;
  bool_value_ = val;
}

void Value::SetInteger(int64 val) {
  type_ = Type::INTEGER;
  int_value_ = val;
}

void Value::SetFloat(double val) {
  type_ = Type::FLOAT;
  float_value_ = val;
}

void Value::SetString(std::string_view val) {
  type_ = Type::STRING;
  string_value_ = std::string(val);
}

void Value::SetArray(std::vector<Value> val) {
  type_ = Type::ARRAY;
  array_value_ = std::move(val);
}

void Value::SetUndefined() {
  type_ = Type::UNDEFINED;
  bool_value_ = false;
  int_value_ = 0;
  float_value_ = 0.0;
  string_value_.clear();
  array_value_.clear();
}

void Value::Serialize(Serializer& serializer) {
  if (serializer.IsDeserializing()) {
    type_ = Type::UNDEFINED;

    if (serializer.HasThisField("bool_value")) {
      serializer.Integer("bool_value", bool_value_);
      type_ = Type::BOOLEAN;
    } else {
      serializer.Integer();
    }

    if (serializer.HasThisField("int_value")) {
      serializer.Integer("int_value", int_value_);
      type_ = Type::INTEGER;
    } else {
      serializer.Integer();
    }

    if (serializer.HasThisField("float_value")) {
      serializer.Double("float_value", float_value_);
      type_ = Type::FLOAT;
    } else {
      serializer.Double();
    }

    if (serializer.HasThisField("string_value")) {
      serializer.String("string_value", string_value_);
      type_ = Type::STRING;
    } else {
      serializer.String();
    }

    if (serializer.HasThisField("array_value")) {
      serializer.ArrayOfSerializables("array_value", array_value_);
      type_ = Type::ARRAY;
    } else {
      serializer.ArrayOfSerializables();
    }
  } else {
    if (type_ == Type::BOOLEAN) {
      serializer.Integer("bool_value", bool_value_);
    } else {
      serializer.Integer();
    }

    if (type_ == Type::INTEGER) {
      serializer.Integer("int_value", int_value_);
    } else {
      serializer.Integer();
    }

    if (type_ == Type::FLOAT) {
      serializer.Double("float_value", float_value_);
    } else {
      serializer.Double();
    }

    if (type_ == Type::STRING) {
      serializer.String("string_value", string_value_);
    } else {
      serializer.String();
    }

    if (type_ == Type::ARRAY) {
      serializer.ArrayOfSerializables("array_value", array_value_);
    } else {
      serializer.ArrayOfSerializables();
    }
  }
}

}  // namespace serialization
}  // namespace perception
