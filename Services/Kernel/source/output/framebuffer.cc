#ifndef TEST
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

#include "output/framebuffer.h"

#include "../../../third_party/multiboot2.h"
#include "output/text_terminal.h"
#include "memory/virtual_allocator.h"

namespace output {

using memory::kVirtualMemoryOffset;

namespace {

// Frame buffer details saved from the multiboot header.
size_t g_framebuffer_address;
uint32 g_framebuffer_width;
uint32 g_framebuffer_height;
uint32 g_framebuffer_pitch;
uint8 g_framebuffer_bits_per_pixel;

// Initializes the framebuffer details.
void SetFramebufferDetails(size_t address, uint32 width, uint32 height,
                           uint32 pitch, uint8 bpp) {
  g_framebuffer_address = address;
  g_framebuffer_width = width;
  g_framebuffer_height = height;
  g_framebuffer_pitch = pitch;
  g_framebuffer_bits_per_pixel = bpp;
}

}  // namespace

void MaybeLoadFramebuffer() {
  // Initialize to empty values, in case a framebuffer isn't found in the
  // multiboot header.
  g_framebuffer_address = 0;
  g_framebuffer_width = 0;
  g_framebuffer_height = 0;
  g_framebuffer_pitch = 0;
  g_framebuffer_bits_per_pixel = 0;

  // Now in higher half memory, so kVirtualMemoryOffset must be added.
  multiboot_info* higher_half_multiboot_info =
      (multiboot_info*)((size_t)&MultibootInfo + kVirtualMemoryOffset);

  size_t multiboot_address =
      higher_half_multiboot_info->addr + kVirtualMemoryOffset;
  size_t multiboot_end = multiboot_address + *(uint32*)multiboot_address;

  // Loop through the multiboot sections.
  for (multiboot_tag* tag = (multiboot_tag*)(size_t)(higher_half_multiboot_info->addr + 8 +
                                      kVirtualMemoryOffset);
       tag != nullptr && (size_t)tag + sizeof(multiboot_tag) <= multiboot_end &&
       tag->type != MULTIBOOT_TAG_TYPE_END;
       tag = (tag->size < 8) ? nullptr : (multiboot_tag*)((size_t)tag + (size_t)((tag->size + 7) & ~7))) {
    // Found a framebuffer.
    if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
      multiboot_tag_framebuffer *tagfb = (multiboot_tag_framebuffer *)tag;
      if (tagfb->common.framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
        SetFramebufferDetails(
            tagfb->common.framebuffer_addr, tagfb->common.framebuffer_width,
            tagfb->common.framebuffer_height, tagfb->common.framebuffer_pitch,
            tagfb->common.framebuffer_bpp);
      } else {
        print << "Found a VESA framebuffer tag, but the framebuffer "
                 "is not of type MULTIBOOT_FRAMEBUFFER_TYPE_RGB.\n";
      }
    }
  }
}

void PopulateRegistersWithFramebufferDetails(hardware::Registers& regs) {
  regs.rax = g_framebuffer_address;
  regs.rbx = (size_t)g_framebuffer_width;
  regs.rdx = (size_t)g_framebuffer_height;
  regs.rsi = (size_t)g_framebuffer_pitch;
  regs.r8 = (size_t)g_framebuffer_bits_per_pixel;
}

}  // namespace output

#endif // TEST
