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

#include "perception/serialization/binary_serializer.h"

#include <memory>
#include <vector>

#include "perception/serialization/binary_deserializer.h"
#include "perception/serialization/memory_read_stream.h"
#include "perception/serialization/serializable.h"
#include "perception/serialization/serializer.h"
#include "perception/serialization/vector_write_stream.h"
#include "testing.h"

namespace {

class InnerTestObject : public ::perception::serialization::Serializable {
 public:
  int integer_val = 0;
  std::string string_val;

  virtual void Serialize(
      ::perception::serialization::Serializer& serializer) override {
    serializer.Integer("integer_val", integer_val);
    serializer.String("string_val", string_val);
  }
};

class TestObject : public ::perception::serialization::Serializable {
 public:
  bool bool_val = false;
  int int_val = 0;
  double double_val = 0.0;
  std::string string_val;
  std::shared_ptr<InnerTestObject> inner_obj;
  std::vector<std::shared_ptr<InnerTestObject>> array_val;
  std::vector<std::string> string_array_val;

  virtual void Serialize(
      ::perception::serialization::Serializer& serializer) override {
    serializer.Integer("bool_val", bool_val);
    serializer.Integer("int_val", int_val);
    serializer.Double("double_val", double_val);
    serializer.String("string_val", string_val);
    serializer.Serializable("inner_obj", inner_obj);
    serializer.ArrayOfSerializables("array_val", array_val);
    serializer.ArrayOfStrings("string_array_val", string_array_val);
  }
};

}  // namespace

TEST(BinarySerializationRoundtrip) {
  TestObject obj1;
  obj1.bool_val = true;
  obj1.int_val = -12345;
  obj1.double_val = 3.14159;
  obj1.string_val = "Hello\nWorld \"with quotes\" and \\ backslashes!";

  obj1.inner_obj = std::make_shared<InnerTestObject>();
  obj1.inner_obj->integer_val = 9876;
  obj1.inner_obj->string_val = "Nested string";

  auto item1 = std::make_shared<InnerTestObject>();
  item1->integer_val = 111;
  item1->string_val = "array item 1";
  obj1.array_val.push_back(item1);

  auto item2 = std::make_shared<InnerTestObject>();
  item2->integer_val = 222;
  item2->string_val = "array item 2";
  obj1.array_val.push_back(item2);

  obj1.string_array_val.push_back("array string 1");
  obj1.string_array_val.push_back("array string 2");
  obj1.string_array_val.push_back("");
  obj1.string_array_val.push_back(
      "array string 4 with \"quotes\" and \\ backslashes!");

  std::vector<std::byte> serialized =
      ::perception::serialization::SerializeToByteVector(obj1);

  // Deserialize into obj2
  TestObject obj2;
  ::perception::serialization::DeserializeFromByteVector(obj2, serialized);

  EXPECT(obj1.bool_val, obj2.bool_val);
  EXPECT(obj1.int_val, obj2.int_val);
  EXPECT(obj1.double_val, obj2.double_val);
  EXPECT(obj1.string_val, obj2.string_val);

  EXPECT(true, obj2.inner_obj != nullptr);
  if (obj2.inner_obj) {
    EXPECT(obj1.inner_obj->integer_val, obj2.inner_obj->integer_val);
    EXPECT(obj1.inner_obj->string_val, obj2.inner_obj->string_val);
  }

  EXPECT((size_t)2, obj2.array_val.size());
  if (obj2.array_val.size() == 2) {
    EXPECT(111, obj2.array_val[0]->integer_val);
    EXPECT(std::string("array item 1"), obj2.array_val[0]->string_val);
    EXPECT(222, obj2.array_val[1]->integer_val);
    EXPECT(std::string("array item 2"), obj2.array_val[1]->string_val);
  }

  EXPECT((size_t)4, obj2.string_array_val.size());
  if (obj2.string_array_val.size() == 4) {
    EXPECT(std::string("array string 1"), obj2.string_array_val[0]);
    EXPECT(std::string("array string 2"), obj2.string_array_val[1]);
    EXPECT(std::string(""), obj2.string_array_val[2]);
    EXPECT(std::string("array string 4 with \"quotes\" and \\ backslashes!"),
           obj2.string_array_val[3]);
  }
}
