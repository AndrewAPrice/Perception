

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
#include "perception/serialization/binary_deserializer.h"

#include <types.h>

#include <algorithm>
#include <climits>
#include <string>

#include "perception/serialization/memory_read_stream.h"
#include "perception/serialization/read_stream.h"
#include "perception/serialization/serializable.h"
#include "perception/serialization/serializer.h"

namespace perception {
namespace serialization {

namespace {
// Sentinel value indicating that the end of the stream has been reached and no
// more fields are present.
constexpr int kEndOfStreamFieldIndex = -1;

// Maximum shift in bits when decoding a 64-bit variable-length integer.
constexpr unsigned int kMaxVarIntShift = 70;

uint64 ReadVariableLengthIntegerFromStream(ReadStream& read_stream) {
  uint64_t result = 0;
  unsigned int shift = 0;

  while (true) {
    if (shift >= kMaxVarIntShift || read_stream.HasReachedEndOfStream()) {
      // An encoded 64-bit integer can be at most 10 bytes long (since 10 * 7 =
      // 70 bits). If the result is shifted more than 63 bits, the data is
      // malformed or represents a number larger than uint64_t.
      return result;
    }

    uint8_t byte;
    read_stream.CopyDataOutOfStream(&byte, 1);

    // Take the lower 7 bits of the byte, cast to uint64_t to prevent overflow
    // during the left shift, shift the 7 bits into their correct position in
    // the result, and combine with previously processed bits.
    result |= static_cast<uint64_t>(byte & 0x7F) << shift;

    // Check the continuation bit (MSB). If it's 0, this is the last byte.
    if ((byte & 0x80) == 0)
      return result;

    // Increment shift for the next 7-bit chunk.
    shift += 7;
  }
}

int ReadNextFieldIndexFromStream(ReadStream& read_stream) {
  if (read_stream.HasReachedEndOfStream())
    return kEndOfStreamFieldIndex;
  uint64 field_index = ReadVariableLengthIntegerFromStream(read_stream);
  if (field_index > static_cast<uint64>(INT_MAX))
    return kEndOfStreamFieldIndex;
  return static_cast<int>(field_index);
}

class BinaryDeserializer : public Serializer {
 public:
  BinaryDeserializer(ReadStream* read_stream)
      : read_stream_(read_stream),
        current_field_index_(0),
        next_field_index_in_stream_(ReadNextFieldIndex()) {}

  virtual bool HasThisField(std::string_view name = "") override {
    return next_field_index_in_stream_ == current_field_index_;
  }

  virtual bool IsDeserializing() override {
    // Only deserializing is supported.
    return true;
  }

  virtual void Integer() override {
    if (HasThisField()) {
      (void)ReadVariableLengthInteger();
      next_field_index_in_stream_ = ReadNextFieldIndex();
    }
    current_field_index_++;
  }

  virtual void UnsignedInteger(std::string_view name, uint64& value) override {
    if (HasThisField()) {
      value = ReadVariableLengthInteger();
      next_field_index_in_stream_ = ReadNextFieldIndex();
    } else {
      value = 0;
    }
    current_field_index_++;
  }

  virtual void SignedInteger(std::string_view name, int64& value) override {
    if (HasThisField()) {
      value = ReadVariableLengthSignedInteger();
      next_field_index_in_stream_ = ReadNextFieldIndex();
    } else {
      value = 0;
    }
    current_field_index_++;
  }

  virtual void Float() override {
    if (HasThisField()) {
      read_stream_->SkipForward(sizeof(float));
      next_field_index_in_stream_ = ReadNextFieldIndex();
    }
    current_field_index_++;
  }

  virtual void Float(std::string_view name, float& value) override {
    if (HasThisField()) {
      read_stream_->CopyDataOutOfStream(&value, sizeof(float));
      next_field_index_in_stream_ = ReadNextFieldIndex();
    } else {
      value = 0;
    }
    current_field_index_++;
  }

  virtual void Double() override {
    if (HasThisField()) {
      read_stream_->SkipForward(sizeof(double));
      next_field_index_in_stream_ = ReadNextFieldIndex();
    }
    current_field_index_++;
  }

  virtual void Double(std::string_view name, double& value) override {
    if (HasThisField()) {
      read_stream_->CopyDataOutOfStream(&value, sizeof(double));
      next_field_index_in_stream_ = ReadNextFieldIndex();
    } else {
      value = 0;
    }
    current_field_index_++;
  }

  virtual void String() override {
    if (HasThisField()) {
      uint64 string_length = ReadVariableLengthInteger();
      read_stream_->SkipForward(string_length);
      next_field_index_in_stream_ = ReadNextFieldIndex();
    }
    current_field_index_++;
  }

  virtual void String(std::string_view name, std::string& str) override {
    if (HasThisField()) {
      uint64 string_length = ReadVariableLengthInteger();
      size_t clamped_length = static_cast<size_t>(
          std::min<uint64>(string_length, read_stream_->RemainingBytes()));
      str.resize(clamped_length);
      read_stream_->CopyDataOutOfStream(&str[0], clamped_length);
      if (string_length > clamped_length)
        read_stream_->SkipForward(string_length - clamped_length);
      next_field_index_in_stream_ = ReadNextFieldIndex();
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
      next_field_index_in_stream_ = ReadNextFieldIndex();
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
      next_field_index_in_stream_ = ReadNextFieldIndex();
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
        uint64 raw_elements = ReadVariableLengthIntegerFromStream(sub_stream);
        uint64 max_elements = sub_stream.RemainingBytes() / sizeof(uint32);
        int elements = static_cast<int>(std::min<uint64>(
            raw_elements,
            std::min<uint64>(max_elements, static_cast<uint64>(INT_MAX))));

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
      next_field_index_in_stream_ = ReadNextFieldIndex();
    } else {
      deserialization_function(0, [](class Serializable& serializable) {});
    }
    current_field_index_++;
  }

  virtual void ArrayOfStrings() override { Serializable(); }

  virtual void ArrayOfStrings(std::string_view name,
                              std::vector<std::string>& arr) override {
    if (HasThisField()) {
      uint32 size;
      read_stream_->CopyDataOutOfStream(&size, 4);
      read_stream_->ReadSubStream(size, [&arr](ReadStream& sub_stream) {
        uint64 raw_elements = ReadVariableLengthIntegerFromStream(sub_stream);
        size_t elements = static_cast<size_t>(std::min<uint64>(
            raw_elements,
            std::min<uint64>(sub_stream.RemainingBytes(),
                             static_cast<uint64>(INT_MAX))));
        arr.resize(elements);
        for (size_t i = 0; i < elements; i++) {
          uint64 string_length =
              ReadVariableLengthIntegerFromStream(sub_stream);
          size_t clamped_length = static_cast<size_t>(
              std::min<uint64>(string_length, sub_stream.RemainingBytes()));
          arr[i].resize(clamped_length);
          sub_stream.CopyDataOutOfStream(&arr[i][0], clamped_length);
          if (string_length > clamped_length)
            sub_stream.SkipForward(string_length - clamped_length);
        }
      });
      next_field_index_in_stream_ = ReadNextFieldIndex();
    } else {
      arr.clear();
    }
    current_field_index_++;
  }

 private:
  uint64 ReadVariableLengthInteger() {
    return ReadVariableLengthIntegerFromStream(*read_stream_);
  }

  int ReadNextFieldIndex() {
    return ReadNextFieldIndexFromStream(*read_stream_);
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