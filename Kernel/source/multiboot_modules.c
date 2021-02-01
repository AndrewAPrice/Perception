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

#include "multiboot_modules.h"

#include "elf_loader.h"
#include "../../third_party/multiboot2.h"
#include "text_terminal.h"
#include "virtual_allocator.h"

void LoadMultibootModules() {
	// We are now in higher half memory, so we have to add VIRTUAL_MEMORY_OFFSET.
	struct multiboot_info* higher_half_multiboot_info =
		(struct multiboot_info *)((size_t)&MultibootInfo + VIRTUAL_MEMORY_OFFSET);

	// Loop through the multiboot sections.
	struct multiboot_tag *tag;
	for(tag = (struct multiboot_tag *)(size_t)(higher_half_multiboot_info->addr + 8 + VIRTUAL_MEMORY_OFFSET);
		tag->type != MULTIBOOT_TAG_TYPE_END;
		tag = (struct multiboot_tag *)((size_t) tag + (size_t)((tag->size + 7) & ~7))) {

		// Found a multiboot module.
		if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
			struct multiboot_tag_module *module_tag = (struct multiboot_tag_module *)tag;

			LoadElfProcess(module_tag->mod_start + VIRTUAL_MEMORY_OFFSET,
				module_tag->mod_end + VIRTUAL_MEMORY_OFFSET,
				module_tag->cmdline);
		}
	}
}