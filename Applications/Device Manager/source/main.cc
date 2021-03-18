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

#include "device_manager.h"
#include "driver_loader.h"
#include "pci.h"
#include "perception/framebuffer.h"
#include "perception/scheduler.h"

#include "types.h"

using ::perception::GetMultibootFramebufferDetails;
using ::perception::HandOverControl;

namespace {

void LoadVideoDriver() {
	size_t physical_address;
	uint32 width, height, pitch;
	uint8 bpp;
	GetMultibootFramebufferDetails(physical_address,
		width, height, pitch, bpp);

	if (width == 0) {
		// No multi-boot framebuffer was set up by the bootloader. We
		// will fallback to the VGA driver.
		AddDriverToLoad("VGA Driver");
	} else {
		AddDriverToLoad("Multiboot Framebuffer");
	}
}

}

int main() {
	InitializePci();

	AddDriverToLoad("PS2 Keyboard and Mouse");
	LoadVideoDriver();
	
	LoadAllRemainingDrivers();

	auto device_manager = std::make_unique<DeviceManager>();

	HandOverControl();

	return 0;
}

