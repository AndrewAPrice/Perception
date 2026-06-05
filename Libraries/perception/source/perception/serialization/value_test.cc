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

#include "testing.h"

namespace {

using ::perception::serialization::Value;

TEST(ValueConstructorsAndAccessors) {
  // Undefined
  Value v1;
  EXPECT(Value::Type::UNDEFINED, v1.GetType());
  EXPECT(false, v1.BoolValue().has_value());
  EXPECT(false, v1.IntegerValue().has_value());
  EXPECT(false, v1.FloatValue().has_value());
  EXPECT(false, v1.StringValue().has_value());
  EXPECT(true, v1.ArrayValue() == nullptr);

  // Boolean
  Value v2(true);
  EXPECT(Value::Type::BOOLEAN, v2.GetType());
  EXPECT(true, v2.BoolValue().value_or(false));
  EXPECT(false, v2.IntegerValue().has_value());

  Value v3(false);
  EXPECT(Value::Type::BOOLEAN, v3.GetType());
  EXPECT(false, v3.BoolValue().value_or(true));

  // Integer
  Value v4(123456);
  EXPECT(Value::Type::INTEGER, v4.GetType());
  EXPECT((int64)123456, v4.IntegerValue().value_or(0));

  Value v5(-789);
  EXPECT(Value::Type::INTEGER, v5.GetType());
  EXPECT((int64)-789, v5.IntegerValue().value_or(0));

  // Float
  Value v6(3.14159);
  EXPECT(Value::Type::FLOAT, v6.GetType());
  EXPECT(3.14159, v6.FloatValue().value_or(0.0));

  // String
  Value v8("Hello World");
  EXPECT(Value::Type::STRING, v8.GetType());
  EXPECT(std::string_view("Hello World"), v8.StringValue().value_or(""));

  // Array
  std::vector<Value> items = {Value(1), Value("two"), Value(3.3)};
  Value v10(items);
  EXPECT(Value::Type::ARRAY, v10.GetType());
  const auto* arr = v10.ArrayValue();
  EXPECT(true, arr != nullptr);
  if (arr) {
    EXPECT((size_t)3, arr->size());
    EXPECT((int64)1, (*arr)[0].IntegerValue().value_or(0));
    EXPECT(std::string_view("two"), (*arr)[1].StringValue().value_or(""));
    EXPECT(3.3, (*arr)[2].FloatValue().value_or(0.0));
  }
}

}  // namespace
