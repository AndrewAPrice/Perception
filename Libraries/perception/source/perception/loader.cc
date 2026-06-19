// Copyright 2021 Google LLC
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

#include "perception/loader.h"

#include "perception/serialization/serializer.h"

namespace perception {

namespace {

struct StringSerializable : public serialization::Serializable {
  std::string* value;
  StringSerializable(std::string* value) : value(value) {}
  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.String("Value", *value);
  }
};

}  // namespace

void LoadApplicationRequest::Serialize(
    serialization::Serializer& serializer) {
  serializer.String("Name", name);
  serializer.ArrayOfSerializables(
      "Arguments", arguments.size(),
      [this](const std::function<void(class serialization::Serializable&)>&
                 serialize_entry) {
        for (auto& argument : arguments) {
          StringSerializable entry(&argument);
          serialize_entry(entry);
        }
      },
      [this](int elements,
             const std::function<void(class serialization::Serializable&)>&
                 deserialize_entry) {
        arguments.resize(elements);
        for (int i = 0; i < elements; ++i) {
          StringSerializable entry(&arguments[i]);
          deserialize_entry(entry);
        }
      });
}

void LoadApplicationResponse::Serialize(
    serialization::Serializer& serializer) {
  serializer.Integer("Process", process);
}

void GetMultibootRegistryFileRequest::Serialize(
    serialization::Serializer& serializer) {}

void GetMultibootRegistryFileResponse::Serialize(
    serialization::Serializer& serializer) {
  serializer.Integer("RegistryFileId", registry_file_id);
}

}  // namespace perception