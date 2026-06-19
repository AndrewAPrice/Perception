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

#pragma once

#include <string>
#include <vector>

#include "perception/serialization/serializable.h"
#include "perception/service_macros.h"
#include "perception/shared_memory.h"
#include "types.h"

namespace perception {
namespace serialization {
class Serializer;
}

class LoadApplicationRequest : public serialization::Serializable {
 public:
  // The name or path of the application to load.
  std::string name;

  // Optional arguments to pass to the application.
  std::vector<std::string> arguments;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class LoadApplicationResponse : public serialization::Serializable {
 public:
  ProcessId process;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class GetMultibootRegistryFileRequest : public serialization::Serializable {
 public:
  virtual void Serialize(serialization::Serializer& serializer) override;
};

class GetMultibootRegistryFileResponse : public serialization::Serializable {
 public:
  size_t registry_file_id;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

#define METHOD_LIST(X)                                                     \
  X(1, LaunchApplication, LoadApplicationResponse, LoadApplicationRequest) \
  X(2, GetMultibootRegistryFile, GetMultibootRegistryFileResponse,         \
    GetMultibootRegistryFileRequest)
DEFINE_PERCEPTION_SERVICE(Loader, "perception.devices.Loader", METHOD_LIST)
#undef METHOD_LIST

}  // namespace perception
