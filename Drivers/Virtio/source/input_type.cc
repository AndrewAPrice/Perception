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

#include "input_type.h"

#include <cstring>

#include "perception/port_io.h"
#include "virtio_pci_device.h"

using ::perception::Read8BitsFromPort;
using ::perception::Write8BitsToPort;
using ::perception::devices::PciDevice;

VirtioInputType DetectVirtioInputType(const PciDevice& device) {
  VirtioPciDevice virtio_pci(device);
  if (!virtio_pci.Initialize()) {
    return VirtioInputType::Tablet;
  }

  volatile uint8* dev_cfg = virtio_pci.device_cfg();
  if (dev_cfg != nullptr) {
    dev_cfg[0] = 1;  // select = VIRTIO_INPUT_CFG_ID_NAME
    dev_cfg[1] = 0;  // subsel = 0
    char name_buf[32] = {};
    for (int i = 0; i < 31; i++) {
      name_buf[i] = dev_cfg[8 + i];
    }
    name_buf[31] = '\0';

    // EV_ABS check
    dev_cfg[0] = 3;  // select = VIRTIO_INPUT_CFG_EV_BITS
    dev_cfg[1] = 3;  // subsel = EV_ABS
    uint8 abs_size = dev_cfg[2];

    // EV_REL check
    dev_cfg[0] = 3;  // select = VIRTIO_INPUT_CFG_EV_BITS
    dev_cfg[1] = 2;  // subsel = EV_REL
    uint8 rel_size = dev_cfg[2];

    if (rel_size > 0 && abs_size == 0) {
      return VirtioInputType::Mouse;
    }
    if (strstr(name_buf, "Mouse") != nullptr ||
        strstr(name_buf, "mouse") != nullptr) {
      return VirtioInputType::Mouse;
    }
    return VirtioInputType::Tablet;
  } else if (virtio_pci.io_base() != 0) {
    uint16 io_port_base = virtio_pci.io_base();
    Write8BitsToPort(io_port_base + 20, 1);   // select = VIRTIO_INPUT_CFG_ID_NAME
    Write8BitsToPort(io_port_base + 21, 0);   // subsel = 0
    char name_buf[32] = {};
    for (int i = 0; i < 31; i++) {
      name_buf[i] = Read8BitsFromPort(io_port_base + 28 + i);
    }
    name_buf[31] = '\0';

    Write8BitsToPort(io_port_base + 20, 3);   // select = VIRTIO_INPUT_CFG_EV_BITS
    Write8BitsToPort(io_port_base + 21, 3);   // subsel = EV_ABS
    uint8 abs_size = Read8BitsFromPort(io_port_base + 22);

    Write8BitsToPort(io_port_base + 20, 3);   // select = VIRTIO_INPUT_CFG_EV_BITS
    Write8BitsToPort(io_port_base + 21, 2);   // subsel = EV_REL
    uint8 rel_size = Read8BitsFromPort(io_port_base + 22);

    if (rel_size > 0 && abs_size == 0) return VirtioInputType::Mouse;
    if (strstr(name_buf, "Mouse") != nullptr ||
        strstr(name_buf, "mouse") != nullptr) {
      return VirtioInputType::Mouse;
    }
  }

  return VirtioInputType::Tablet;
}
