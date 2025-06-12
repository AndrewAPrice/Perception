

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

#include "perception/serialization/memory_read_stream.h"
#include "perception/serialization/read_stream.h"
#include "perception/serialization/serializable.h"
#include "perception/serialization/serializer.h"
#include "perception/shared_memory.h"

namespace perception {
namespace serialization {

namespace {
uint64 ReadVariableLengthIntegerFromStream(ReadStream& read_stream) {
  uint64_t result = 0;
  unsigned int shift = 0;

  while (true) {
    if (shift >= 70) {
      // An encoded 64-bit integer can be at most 10 bytes long (since 10 * 7 =
      // 70 bits). If the result is shift more than 63 bits, the data is
      // malformed or represents a number larger than uint64_t.
      return result;
    }

    uint8_t byte;
    read_stream.CopyDataOutOfStream(&byte, 1);

    // 1. Take the lower 7 bits of the byte.
    // 2. Cast to uint64_t to prevent overflow during the left shift.
    // 3. Shift the 7 bits into their correct position in the result.
    // 4. Use bitwise OR to combine with previously processed bits.
    result |= static_cast<uint64_t>(byte & 0x7F) << shift;

    // Check the continuation bit (MSB). If it's 0, this is the last byte.
    if ((byte & 0x80) == 0) {
      return result;
    }

    // Increment shift for the next 7-bit chunk.
    shift += 7;
  }
}

class BinaryDeserializer : public Serializer {
 public:
  BinaryDeserializer(ReadStream* read_stream)
      : read_stream_(read_stream),
        current_field_index_(0),
        next_field_index_in_stream_(ReadVariableLengthInteger()) {}

  virtual bool HasThisField() override {
    return next_field_index_in_stream_ == current_field_index_;
  }

  virtual bool IsDeserializing() override {
    // Only deserializing is supported.
    return true;
  }

  virtual void Integer() override {
    if (HasThisField()) {
      (void)ReadVariableLengthInteger();
      next_field_index_in_stream_ = ReadVariableLengthInteger();
    }
    current_field_index_++;
  }

  virtual void Integer(std::string_view name, uint8& value) override {
    uint64 value64;
    Integer(name, value64);
    value = static_cast<uint8>(value64);
  }

  virtual void Integer(std::string_view name, int8& value) override {
    int64 value64;
    Integer(name, value64);
    value = static_cast<int8>(value64);
  }

  virtual void Integer(std::string_view name, uint16& value) override {
    uint64 value64;
    Integer(name, value64);
    value = static_cast<uint16>(value64);
  }

  virtual void Integer(std::string_view name, int16& value) override {
    int64 value64;
    Integer(name, value64);
    value = static_cast<int16>(value64);
  }

  virtual void Integer(std::string_view name, uint32& value) override {
    uint64 value64;
    Integer(name, value64);
    value = static_cast<uint32>(value64);
  }

  virtual void Integer(std::string_view name, int32& value) override {
    int64 value64;
    Integer(name, value64);
    value = static_cast<int32>(value64);
  }

  virtual void Integer(std::string_view name, uint64& value) override {
    if (HasThisField()) {
      value = ReadVariableLengthInteger();
      next_field_index_in_stream_ = ReadVariableLengthInteger();
    } else {
      value = 0;
    }
    current_field_index_++;
  }

  virtual void Integer(std::string_view name, int64& value) override {
    if (HasThisField()) {
      value = ReadVariableLengthSignedInteger();
      next_field_index_in_stream_ = ReadVariableLengthInteger();
    } else {
      value = 0;
    }
    current_field_index_++;
  }

  virtual void Float() override {
    if (HasThisField()) {
      read_stream_->SkipForward(sizeof(float));
      next_field_index_in_stream_ = ReadVariableLengthInteger();
    }
    current_field_index_++;
  }

  virtual void Float(std::string_view name, float& value) override {
    if (HasThisField()) {
      read_stream_->CopyDataOutOfStream(&value, sizeof(float));
      next_field_index_in_stream_ = ReadVariableLengthInteger();
    } else {
      value = 0;
    }
    current_field_index_++;
  }

  virtual void Double() override {
    if (HasThisField()) {
      read_stream_->SkipForward(sizeof(double));
      next_field_index_in_stream_ = ReadVariableLengthInteger();
    }
    current_field_index_++;
  }

  virtual void Double(std::string_view name, double& value) override {
    if (HasThisField()) {
      read_stream_->CopyDataOutOfStream(&value, sizeof(double));
      next_field_index_in_stream_ = ReadVariableLengthInteger();
    } else {
      value = 0;
    }
    current_field_index_++;
  }

  virtual void String() override {
    if (HasThisField()) {
      uint64 string_length = ReadVariableLengthInteger();
      read_stream_->SkipForward(string_length);
      next_field_index_in_stream_ = ReadVariableLengthInteger();
    }
    current_field_index_++;
  }

  virtual void String(std::string_view name, std::string& str) override {
    if (HasThisField()) {
      uint64 string_length = ReadVariableLengthInteger();
      str.resize(string_length);
      read_stream_->CopyDataOutOfStream(&str[0], string_length);
      next_field_index_in_stream_ = ReadVariableLengthInteger();
    } else {
      str.clear();
    }

    current_field_index_++;
  }

  virtual void Serializable() override {
    if (HasThisField()) {
      uint32 size;
      read_stream_->CopyDataOutOfStream(&size, 4);
      read_stream_->SkipForward(size);
      next_field_index_in_stream_ = ReadVariableLengthInteger();
    }
    current_field_index_++;
  }

  virtual void Serializable(std::string_view name,
                            class Serializable& obj) override {
    if (HasThisField()) {
      uint32 size;
      read_stream_->CopyDataOutOfStream(&size, 4);
      read_stream_->ReadSubStream(size, [&obj](ReadStream& sub_stream) {
        BinaryDeserializer sub_serializer(&sub_stream);
        obj.Serialize(sub_serializer);
      });
      next_field_index_in_stream_ = ReadVariableLengthInteger();
    } else {
      read_stream_->ReadSubStream(0, [&obj](ReadStream& sub_stream) {
        BinaryDeserializer sub_serializer(&sub_stream);
        obj.Serialize(sub_serializer);
      });
    }
    current_field_index_++;
  }

  virtual void ArrayOfSerializables() override {
    // The first thing encoded is the byte size of the entire array, an array
    // can be skipped over in the same way as a serializable.
    Serializable();
  }

  virtual void ArrayOfSerializables(
      std::string_view name, int current_size,
      const std::function<
          void(const std::function<void(class Serializable& serializable)>&
                   serialize_entry)>& serialization_function,
      const std::function<
          void(int elements,
               const std::function<void(class Serializable& serializable)>&
                   deserialize_entry)>& deserialization_function) override {
    if (HasThisField()) {
      uint32 size;
      read_stream_->CopyDataOutOfStream(&size, 4);
      // Read through the array in a self-contained sub stream incase something
      // is malformed and while reading the array, either not all bytes are read
      // or it attemps to read past the end of the array.
      read_stream_->ReadSubStream(size, [&deserialization_function](
                                            ReadStream& sub_stream) {
        uint64 elements = ReadVariableLengthIntegerFromStream(sub_stream);

        deserialization_function(
            elements, [&sub_stream](class Serializable& serializable) {
              uint32 element_size;
              sub_stream.CopyDataOutOfStream(&element_size, 4);
              sub_stream.ReadSubStream(
                  element_size, [&serializable](ReadStream& sub_sub_stream) {
                    BinaryDeserializer sub_serializer(&sub_sub_stream);
                    serializable.Serialize(sub_serializer);
                  });
            });
      });
      next_field_index_in_stream_ = ReadVariableLengthInteger();
    } else {
      deserialization_function(0, [](class Serializable& serializable) {});
    }
    current_field_index_++;
  }

 private:
  uint64 ReadVariableLengthInteger() {
    return ReadVariableLengthIntegerFromStream(*read_stream_);
  }

  int64 ReadVariableLengthSignedInteger() {
    uint64_t zigzag_encoded = ReadVariableLengthInteger();
    return (zigzag_encoded >> 1) ^ -static_cast<int64_t>(zigzag_encoded & 1);
  }

  ReadStream* read_stream_;
  int current_field_index_;
  int next_field_index_in_stream_;
};

}  // namespace

void DeserializeFromStream(Serializable& object, ReadStream& stream) {
  BinaryDeserializer serializer(&stream);
  object.Serialize(serializer);
}

}  // namespace serialization
}  // namespace perception