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

#include "perception/framebuffer.h"

namespace perception {

// Gets the details of the framebuffer set up by the bootloader.
void GetMultibootFramebufferDetails(
	size_t& physical_address,
	uint32& width, uint32& height, uint32& pitch,
	uint8& bpp) {
#ifdef PERCEPTION
	volatile register size_t syscall asm ("rdi") = 40;
	volatile register size_t physical_address_r asm ("rax");
	volatile register size_t width_r asm ("rbx");
	volatile register size_t height_r asm ("rdx");
	volatile register size_t pitch_r asm ("rsi");
	volatile register size_t bpp_r asm ("r8");

	__asm__ __volatile__ ("syscall\n":
		"=r"(physical_address_r), "=r"(width_r), "=r"(height_r),
		"=r"(pitch_r), "=r"(bpp_r) : "r" (syscall): "rcx", "r11");

	physical_address = physical_address_r;
	width = (size_t)width_r;
	height = (size_t)height_r;
	pitch = (size_t)pitch_r;
	bpp = (size_t)bpp_r;
#else
	physical_address = 0;
	width = 0;
	height = 0;
	pitch = 0;
	bpp = 0;
#endif
}

}