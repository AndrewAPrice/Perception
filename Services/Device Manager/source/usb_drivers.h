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

#pragma once

#include "perception/devices/usb_device.h"

// Inspects an enumerated USB interface, updates boot input device presence
// flags, and requests loading the matching USB driver if no running driver is
// already handling the interface. Returns true if a driver is known for this
// interface.
bool LoadUsbDriver(
    const ::perception::devices::UsbInterfaceInfo& interface_info,
    bool handled_by_running_driver);
