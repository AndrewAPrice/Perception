

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
#include "perception/serialization/binary_serializer.h"

#include <types.h>

#include <string>

#include "perception/serialization/serializable.h"
#include "perception/serialization/serializer.h"
#include "perception/serialization/write_stream.h"

namespace perception {
namespace serialization {

namespace {
class BinarySerializer : public Serializer {
 public:
  BinarySerializer(WriteStream* write_stream)
      : write_stream_(write_stream), current_field_index_(0) {}

  virtual bool HasThisField() override {
    // Not needed for serializing.
    return false;
  }

  virtual bool IsDeserializing() override {
    // Only serializing is supported.
    return false;
  }

  virtual void Integer() override { current_field_index_++; }

  virtual void UnsignedInteger(std::string_view name, uint64& value) override {
    if (value > 0) {
      WriteVariableLengthInteger(current_field_index_);
      WriteVariableLengthInteger(value);
    }
    current_field_index_++;
  }

  virtual void SignedInteger(std::string_view name, int64& value) override {
    if (value != 0) {
      WriteVariableLengthInteger(current_field_index_);
      WriteVariableLengthSignedInteger(value);
    }
    current_field_index_++;
  }

  virtual void Float() override { current_field_index_++; }

  virtual void Float(std::string_view name, float& value) override {
    if (value != 0) {
      WriteVariableLengthInteger(current_field_index_);
      write_stream_->CopyDataIntoStream(&value, sizeof(float));
    }
    current_field_index_++;
  }

  virtual void Double() override { current_field_index_++; }

  virtual void Double(std::string_view name, double& value) override {
    if (value != 0) {
      WriteVariableLengthInteger(current_field_index_);
      write_stream_->CopyDataIntoStream(&value, sizeof(double));
    }
    current_field_index_++;
  }

  virtual void String() override { current_field_index_++; }

  virtual void String(std::string_view name, std::string& str) override {
    if (!str.empty()) {
      WriteVariableLengthInteger(current_field_index_);
      WriteVariableLengthInteger(str.length());
      write_stream_->CopyDataIntoStream(str.data(), str.length());
    }
    current_field_index_++;
  }

  virtual void Serializable() override { current_field_index_++; }

  virtual void Serializable(std::string_view name,
                            class Serializable& obj) override {
    WriteVariableLengthInteger(current_field_index_);
    SerializeObject(obj);

    current_field_index_++;
  }

  virtual void ArrayOfSerializables() override { current_field_index_++; }

  virtual void ArrayOfSerializables(
      std::string_view name, int current_size,
      const std::function<
          void(const std::function<void(class Serializable& serializable)>&
                   serialize_entry)>& serialization_function,
      const std::function<
          void(int elements,
               const std::function<void(class Serializable& serializable)>&
                   deserialize_entry)>& deserialization_function) override {
    if (current_size > 0) {
      WriteVariableLengthInteger(current_field_index_);

      // Reserve space for the size.
      size_t size_position = write_stream_->CurrentOffset();
      write_stream_->SkipForward(4);
      size_t start_position = size_position + 4;

      // Write length of the array.
      WriteVariableLengthInteger(current_size);

      // Serialize each sub object.
      WriteStream* write_stream = write_stream_;
      serialization_function([this](class Serializable& serializable) {
        this->SerializeObject(serializable);
      });

      // Write the size.
      uint32 size =
          static_cast<uint32>(write_stream->CurrentOffset() - start_position);
      write_stream->CopyDataIntoStream(&size, 4, size_position);
    }
    current_field_index_++;
  }

 private:
  void WriteVariableLengthInteger(uint64 value) {
    while (value >= 0x80) {
      auto byte = static_cast<uint8_t>(value & 0x7F) | 0x80;
      write_stream_->CopyDataIntoStream(&byte, 1);
      value >>= 7;
    }
    auto byte = static_cast<uint8_t>(value);
    write_stream_->CopyDataIntoStream(&byte, 1);
  }

  void WriteVariableLengthSignedInteger(int64 value) {
    uint64_t zigzag_encoded =
        (static_cast<uint64_t>(value) << 1) ^ (value >> 63);
    WriteVariableLengthInteger(zigzag_encoded);
  }

  void SerializeObject(class Serializable& obj) {
    // Reserve space for the size of the sub object.
    size_t size_position = write_stream_->CurrentOffset();
    write_stream_->SkipForward(4);
    size_t start_position = size_position + 4;

    // Serialize the sub object.
    BinarySerializer serializer(write_stream_);
    obj.Serialize(serializer);

    // Write the size of the sub object.
    uint32 size =
        static_cast<uint32>(write_stream_->CurrentOffset() - start_position);
    write_stream_->CopyDataIntoStream(&size, 4, size_position);
  }

  WriteStream* write_stream_;
  int current_field_index_;
};

}  // namespace

void SerializeIntoStream(const Serializable& object, WriteStream& stream) {
  BinarySerializer serializer(&stream);
  const_cast<Serializable&>(object).Serialize(serializer);
}

}  // namespace serialization
}  // namespace perception