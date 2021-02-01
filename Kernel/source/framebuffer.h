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

#include "registers.h"
#include "types.h"

// Normally a microkernel shouldn't care about a specific device such as the
// video card, but multiboot bootloaders have the ability to set a video
// mode and tell the kernel the location of the framebuffer via the multi-boot
// header. The functions here store these details so that a video card
// driver can discover them.

// Maybe load the framebuffer from the multiboot header.
void MaybeLoadFramebuffer();

// Populates the registers with framebuffer details.
extern void PopulateRegistersWithFramebufferDetails(
	struct Registers* regs);