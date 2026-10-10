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

#include "perception/devices/usb_device.h"

#include "perception/serialization/serializer.h"

namespace perception {
namespace devices {

void UsbInterfaceInfo::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Device handle", device_handle);
  serializer.Integer("Vendor ID", vendor_id);
  serializer.Integer("Product ID", product_id);
  serializer.Integer("Device speed", device_speed);
  serializer.Integer("Interface number", interface_number);
  serializer.Integer("Alternate setting", alternate_setting);
  serializer.Integer("Interface class", interface_class);
  serializer.Integer("Interface subclass", interface_subclass);
  serializer.Integer("Interface protocol", interface_protocol);
  serializer.Integer("Interrupt IN endpoint address",
                     interrupt_in_endpoint_address);
  serializer.Integer("Interrupt IN DCI", interrupt_in_dci);
  serializer.Integer("Max packet size", max_packet_size);
  serializer.Integer("Interval", interval);
  serializer.String("HID report descriptor", hid_report_descriptor);
}

void UsbInterfaces::Serialize(serialization::Serializer& serializer) {
  serializer.ArrayOfSerializables("Interfaces", interfaces);
}

void UsbInterfaceFilter::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Interface class", interface_class);
  serializer.Integer("Interface subclass", interface_subclass);
  serializer.Integer("Interface protocol", interface_protocol);
}

void UsbDeviceId::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Device handle", device_handle);
}

void OpenUsbEndpointRequest::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Device handle", device_handle);
  serializer.Integer("Interface number", interface_number);
  serializer.Integer("Endpoint DCI", endpoint_dci);
}

void UsbEndpointRingInfo::Serialize(serialization::Serializer& serializer) {
  serializer.Serializable("Ring memory", ring_memory);
  serializer.Integer("Ring physical address", ring_phys_address);
  serializer.Integer("Doorbell physical address", doorbell_phys_address);
  serializer.Integer("Doorbell value", doorbell_value);
}

void UsbControlTransferRequest::Serialize(
    serialization::Serializer& serializer) {
  serializer.Integer("Device handle", device_handle);
  serializer.Integer("Request type", request_type);
  serializer.Integer("Request", request);
  serializer.Integer("Value", value);
  serializer.Integer("Index", index);
  serializer.Integer("Length", length);
  serializer.String("Data", data);
}

void UsbControlTransferResponse::Serialize(
    serialization::Serializer& serializer) {
  serializer.String("Data", data);
}

}  // namespace devices
}  // namespace perception
