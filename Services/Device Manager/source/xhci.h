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
#include "status.h"
#include "types.h"

// Registers an xHCI PCI controller to be initialized by Device Manager.
void RegisterXhciController(uint8 bus, uint8 slot, uint8 function);

// Initializes all registered xHCI controllers and enumerates initial USB devices.
void InitializeXhciControllers();

// Queries enumerated USB interfaces matching the given filter.
StatusOr<::perception::devices::UsbInterfaces> QueryXhciUsbInterfaces(
    const ::perception::devices::UsbInterfaceFilter& filter);

// Registers a listener to be notified when USB interfaces are attached or detached.
Status RegisterXhciUsbDeviceListener(
    const ::perception::devices::RegisterUsbDeviceListenerRequest& request);

// Configures and opens a shared memory Transfer Ring for a USB interrupt endpoint.
StatusOr<::perception::devices::UsbEndpointRingInfo> OpenXhciInterruptEndpoint(
    const ::perception::devices::OpenUsbEndpointRequest& request);

// Executes a synchronous USB Control Transfer on Endpoint 0 for the specified device.
StatusOr<::perception::devices::UsbControlTransferResponse>
ExecuteXhciControlTransfer(
    const ::perception::devices::UsbControlTransferRequest& request);
