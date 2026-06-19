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

#include "perception/serialization/value_serialization.h"

#include "testing.h"

namespace {

using ::perception::serialization::StringToValue;
using ::perception::serialization::Value;
using ::perception::serialization::ValueToString;

TEST(ValueSerializationToString) {
  // Undefined
  Value v1;
  EXPECT(std::string("null"), ValueToString(v1));

  // Boolean
  Value v2(true);
  EXPECT(std::string("true"), ValueToString(v2));

  Value v3(false);
  EXPECT(std::string("false"), ValueToString(v3));

  // Integer
  Value v4(123456);
  EXPECT(std::string("123456"), ValueToString(v4));

  Value v5(-789);
  EXPECT(std::string("-789"), ValueToString(v5));

  // Float
  Value v6(3.14159);
  EXPECT(std::string("3.14159"), ValueToString(v6));

  Value v7(0.0);
  EXPECT(std::string("0.0"), ValueToString(v7));

  // String
  Value v8("Hello World");
  EXPECT(std::string("\"Hello World\""), ValueToString(v8));

  // String with escaping
  Value v9("Line1\nLine2\tTab\rCarriage\\Backslash\"Quote");
  EXPECT(
      std::string("\"Line1\\nLine2\\tTab\\rCarriage\\\\Backslash\\\"Quote\""),
      ValueToString(v9));

  // Array
  std::vector<Value> items = {Value(1), Value("two"), Value(3.3)};
  Value v10(items);
  EXPECT(std::string("[1, \"two\", 3.3]"), ValueToString(v10));

  // ColorRGB
  Value v_color(static_cast<uint32>(0xFF4E98FF));
  EXPECT(std::string("{\"r\":78,\"g\":152,\"b\":255}"), ValueToString(v_color));
}

TEST(ValueSerializationFromString) {
  // Null
  auto r1 = StringToValue("null");
  EXPECT(true, r1.Ok());
  EXPECT(Value::Type::UNDEFINED, r1->GetType());

  // Boolean
  auto r2 = StringToValue("true");
  EXPECT(true, r2.Ok());
  EXPECT(Value::Type::BOOLEAN, r2->GetType());
  EXPECT(true, r2->BoolValue().value_or(false));

  auto r3 = StringToValue("false");
  EXPECT(true, r3.Ok());
  EXPECT(Value::Type::BOOLEAN, r3->GetType());
  EXPECT(false, r3->BoolValue().value_or(true));

  // Integer
  auto r4 = StringToValue("987654");
  EXPECT(true, r4.Ok());
  EXPECT(Value::Type::INTEGER, r4->GetType());
  EXPECT((int64)987654, r4->IntegerValue().value_or(0));

  auto r5 = StringToValue("-123");
  EXPECT(true, r5.Ok());
  EXPECT(Value::Type::INTEGER, r5->GetType());
  EXPECT((int64)-123, r5->IntegerValue().value_or(0));

  // Float
  auto r6 = StringToValue("0.12345");
  EXPECT(true, r6.Ok());
  EXPECT(Value::Type::FLOAT, r6->GetType());
  EXPECT(0.12345, r6->FloatValue().value_or(0.0));

  auto r7 = StringToValue("-9.9");
  EXPECT(true, r7.Ok());
  EXPECT(Value::Type::FLOAT, r7->GetType());
  EXPECT(-9.9, r7->FloatValue().value_or(0.0));

  // String
  auto r8 = StringToValue("\"Simple string\"");
  EXPECT(true, r8.Ok());
  EXPECT(Value::Type::STRING, r8->GetType());
  EXPECT(std::string_view("Simple string"), r8->StringValue().value_or(""));

  // String with escaping
  auto r9 =
      StringToValue("\"Line1\\nLine2\\tTab\\rCarriage\\\\Backslash\\\"Quote\"");
  EXPECT(true, r9.Ok());
  EXPECT(Value::Type::STRING, r9->GetType());
  EXPECT(std::string("Line1\nLine2\tTab\rCarriage\\Backslash\"Quote"),
         std::string(r9->StringValue().value_or("")));

  // Array
  auto r10 = StringToValue("[1, \"two\", 3.3]");
  EXPECT(true, r10.Ok());
  EXPECT(Value::Type::ARRAY, r10->GetType());
  const auto* arr = r10->ArrayValue();
  EXPECT(true, arr != nullptr);
  if (arr) {
    EXPECT((size_t)3, arr->size());
    EXPECT((int64)1, (*arr)[0].IntegerValue().value_or(0));
    EXPECT(std::string_view("two"), (*arr)[1].StringValue().value_or(""));
    EXPECT(3.3, (*arr)[2].FloatValue().value_or(0.0));
  }

  // Nested Array
  auto r11 = StringToValue("[[1, 2], [3, 4]]");
  EXPECT(true, r11.Ok());
  EXPECT(Value::Type::ARRAY, r11->GetType());
  const auto* outer = r11->ArrayValue();
  EXPECT(true, outer != nullptr);
  if (outer) {
    EXPECT((size_t)2, outer->size());
    const auto* inner1 = (*outer)[0].ArrayValue();
    EXPECT(true, inner1 != nullptr);
    if (inner1) {
      EXPECT((size_t)2, inner1->size());
      EXPECT((int64)1, (*inner1)[0].IntegerValue().value_or(0));
      EXPECT((int64)2, (*inner1)[1].IntegerValue().value_or(0));
    }
  }

  // Trailing comma array
  auto r12 = StringToValue("[1, 2, 3, ]");
  EXPECT(true, r12.Ok());
  const auto* arr12 = r12->ArrayValue();
  EXPECT(true, arr12 != nullptr);
  if (arr12) {
    EXPECT((size_t)3, arr12->size());
  }

  // ColorRGB
  auto r_color = StringToValue("{\"r\":78,\"g\":152,\"b\":255}");
  EXPECT(true, r_color.Ok());
  EXPECT(Value::Type::COLOR_RGB, r_color->GetType());
  EXPECT(static_cast<uint32>(0xFF4E98FF), r_color->ColorRGBValue().value_or(0));

  // Parsing error cases
  EXPECT(false, StringToValue("nul").Ok());
  EXPECT(false, StringToValue("tru").Ok());
  EXPECT(false, StringToValue("falsey").Ok());
  EXPECT(false, StringToValue("\"unclosed").Ok());
  EXPECT(false, StringToValue("\"bad escape \\x\"").Ok());
  EXPECT(false, StringToValue("[1, 2").Ok());
  EXPECT(false, StringToValue("[1, 2,").Ok());
  EXPECT(false, StringToValue("123.456.789").Ok());
}

}  // namespace
