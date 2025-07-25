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

#include "framebuffer.h"

#include "../../../third_party/multiboot2.h"
#include "text_terminal.h"
#include "virtual_allocator.h"

namespace {

// Frame buffer details saved from the multiboot header.
size_t framebuffer_address;
uint32 framebuffer_width;
uint32 framebuffer_height;
uint32 framebuffer_pitch;
uint8 framebuffer_bits_per_pixel;

// Initializes the framebuffer details.
void SetFramebufferDetails(size_t address, uint32 width, uint32 height,
                           uint32 pitch, uint8 bpp) {
  framebuffer_address = address;
  framebuffer_width = width;
  framebuffer_height = height;
  framebuffer_pitch = pitch;
  framebuffer_bits_per_pixel = bpp;
}

}  // namespace

// Maybe load the framebuffer from the multiboot header.
void MaybeLoadFramebuffer() {
  // Initialize to empty values, in case a framebuffer isn't found in the
  // multiboot header.
  framebuffer_address = 0;
  framebuffer_width = 0;
  framebuffer_height = 0;
  framebuffer_pitch = 0;
  framebuffer_bits_per_pixel = 0;

  // Now in higher half memory, so VIRTUAL_MEMORY_OFFSET must be added.
  multiboot_info *higher_half_multiboot_info =
      (multiboot_info *)((size_t)&MultibootInfo + VIRTUAL_MEMORY_OFFSET);

  // Loop through the multiboot sections.
  multiboot_tag *tag;
  for (tag = (multiboot_tag *)(size_t)(higher_half_multiboot_info->addr + 8 +
                                       VIRTUAL_MEMORY_OFFSET);
       tag->type != MULTIBOOT_TAG_TYPE_END;
       tag = (multiboot_tag *)((size_t)tag + (size_t)((tag->size + 7) & ~7))) {
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

// Populates the registers with framebuffer details.
void PopulateRegistersWithFramebufferDetails(Registers *regs) {
  regs->rax = framebuffer_address;
  regs->rbx = (size_t)framebuffer_width;
  regs->rdx = (size_t)framebuffer_height;
  regs->rsi = (size_t)framebuffer_pitch;
  regs->r8 = (size_t)framebuffer_bits_per_pixel;
}
