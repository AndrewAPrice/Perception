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

#include "perception/framebuffer.h"

#include "perception/messages.h"
#include "perception/memory.h"

#include <iostream>

using ::perception::GetMultibootFramebufferDetails;
using ::perception::kPageSize;
using ::perception::MapPhysicalMemory;

int main() {
	size_t physical_address;
	uint32 width, height, pitch;
	uint8 bpp;
	GetMultibootFramebufferDetails(physical_address,
		width, height, pitch, bpp);

	if (width == 0) {
		std::cout << "The bootloader did not set up a framebuffer." <<
			std::endl;
		return 0;
	}

	std::cout << "The bootloader has set up a " << width << "x" <<
		height << " (" << (int)bpp << "-bit) framebuffer." << std::endl;

	void *framebuffer = MapPhysicalMemory(physical_address,
		(width * pitch + kPageSize - 1) / kPageSize);

	for (int i = 0; i < height; i+= 2) {
		memset((void*)((size_t)framebuffer + i * pitch), 0xFF, pitch);
	}

	perception::TransferToEventLoop();
	return 0;
}
