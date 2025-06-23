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
// #define PERCEPTION
#pragma once

#include <string>
#include <vector>

#include "perception/serialization/serializable.h"
#include "perception/service_macros.h"
#include "perception/shared_memory.h"

namespace perception {
namespace serialization {
class Serializer;
}

namespace devices {
class PciDevice : public serialization::Serializable {
 public:
  std::string name;
  uint8 base_class;
  uint8 sub_class;
  uint8 prog_if;
  uint16 vendor;
  uint16 device_id;
  uint8 bus;
  uint8 slot;
  uint8 function;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class PciDevices : public serialization::Serializable {
 public:
  std::vector<PciDevice> devices;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class PciDeviceFilter : public serialization::Serializable {
 public:
  enum class Key {
    BASE_CLASS,
    SUB_CLASS,
    PROG_IF,
    VENDOR,
    DEVICE_ID,
    BUS,
    SLOT,
    FUNCTION
  };

  Key key;
  int32 value;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class PciDeviceFilters : public serialization::Serializable {
 public:
  std::vector<PciDeviceFilter> filters;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

#define METHOD_LIST(X) X(1, QueryPciDevices, PciDevices, PciDeviceFilters)

DEFINE_PERCEPTION_SERVICE(DeviceManager, "perception.devices.DeviceManager",
                          METHOD_LIST)
#undef METHOD_LIST

}  // namespace devices
}  // namespace perception