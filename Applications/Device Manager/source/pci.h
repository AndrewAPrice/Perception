// Copyright 2020 Google LLC
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

#include <functional>

#include "types.h"

void InitializePci();

void ForEachPciDeviceThatMatchesQuery(
    int16 base_class, int16 sub_class, int16 prog_if, int32 vendor,
    int32 device_id, int16 bus, int16 slot, int16 function,
    const std::function<void(uint8, uint8, uint8, uint16, uint16, uint8, uint8,
                             uint8)>& on_each_device);
