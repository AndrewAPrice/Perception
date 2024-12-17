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

#include "../../../third_party/multiboot2.h"
#include "elf_loader.h"
#include "io.h"
#include "physical_allocator.h"
#include "process.h"
#include "text_terminal.h"
#include "virtual_allocator.h"

namespace {

// A tag type to set for multiboot modules that have been loaded, so they are
// skipped later when a process wants to iterate over the unloaded multiboot
// modules.
constexpr uint32 kLoadedMultibootTagType = 0xFFFFFFFF;

// The next multiboot module to pass to a process.
multiboot_tag *next_multiboot_module_to_pass_to_process;

// The number of multiboot modules to pass to a process.
size_t multiboot_modules_to_pass_to_process;

// Whether a module has been passed into at least one process.
bool has_passed_a_module_into_at_least_one_process = false;

// The pid of the process that modules can be passed to. This is so only the
// same process can keep requesting modules.
size_t pid_of_process_that_modules_can_be_passed_to;

// Returns whether a process can be passed a module.
bool CanProcessRequestModule(Process *process) {
  if (has_passed_a_module_into_at_least_one_process) {
    if (pid_of_process_that_modules_can_be_passed_to != process->pid) {
      // This isn't the same process that modules were previously passed ot.
      return false;
    }
  } else {
    // This is the first process that has asked for a module, so remember this
    // process so only the same one can keep requesting modules.
    has_passed_a_module_into_at_least_one_process = true;
    pid_of_process_that_modules_can_be_passed_to = process->pid;
  }

  return true;
}

// Returns the following multiboot tag.
multiboot_tag *NextMultibootTag(multiboot_tag *tag) {
  return (multiboot_tag *)((size_t)tag + (size_t)((tag->size + 7) & ~7));
}

// Loads a multiboot module into a process. `process` and `tag` are inputs. All
// other parameters are outputs.
void LoadMultibootModuleIntoProcess(Process *process, multiboot_tag_module *tag,
                                    size_t &address_and_flags, size_t &size,
                                    char *name) {
  // Parse the command line into the name.
  char *cmdline = tag->cmdline;
  size_t name_length = 0;
  bool is_driver = false;
  bool can_create_processes = false;

  if (!ParseMultibootModuleName(&cmdline, &name_length, &is_driver,
                                &can_create_processes)) {
    // Skip this module because of an invalid name. This shouldn't have happened
    // because LoadElfProcess should have returned true in this case.
    size = 0;
    return;
  }

  CopyString(cmdline, MODULE_NAME_LENGTH, name_length, name);

  // Calculate the size and allocate the virtual memory to copy this multiboot
  // module into.
  size = tag->mod_end - tag->mod_start;
  size_t pages = PagesThatContainBytes(size);
  address_and_flags = AllocateVirtualMemoryInAddressSpace(
      &process->virtual_address_space, pages);
  if (address_and_flags == 0) {
    print << "Out of memory, can't pass module " << name << " to "
          << process->name << ".\n";
    size = 0;
    return;
  }

  // Copy the multiboot module into the process's virtual memory.
  CopyKernelMemoryIntoProcess(tag->mod_start + VIRTUAL_MEMORY_OFFSET,
                              address_and_flags, address_and_flags + size,
                              process);

  // Attach the flags into the start address.
  if (is_driver) address_and_flags |= 1;
  if (can_create_processes) address_and_flags |= 2;
}

}  // namespace

bool ParseMultibootModuleName(char **name, size_t *name_length, bool *is_driver,
                              bool *can_create_processes) {
  *name_length = strlen(*name);

  while (true) {
    if (*name_length == 0) return false;  // Out of letters.

    if (**name == ' ') {
      // Reached a space.

      // Jumped over the space.
      (*name_length)--;
      (*name)++;

      // A valid name if we still have at least 1 character.
      return *name_length >= 1;
    }

    // Switch over this permission.
    switch (**name) {
      case 'd':
        *is_driver = true;
        break;
      case 'l':
        *can_create_processes = true;
        break;
      case '-':
        break;
      default:
        print << "Unknown attribute '" << **name << "'.";
        return false;
    }

    // Jump over this character.
    (*name)++;
    (*name_length)--;
  }
}

void LoadMultibootModules() {
  // We are now in higher half memory, so we have to add VIRTUAL_MEMORY_OFFSET.
  multiboot_info *higher_half_multiboot_info =
      (multiboot_info *)((size_t)&MultibootInfo + VIRTUAL_MEMORY_OFFSET);

  multiboot_modules_to_pass_to_process = 0;
  next_multiboot_module_to_pass_to_process =
      (multiboot_tag *)(size_t)(higher_half_multiboot_info->addr + 8 +
                                VIRTUAL_MEMORY_OFFSET);
  has_passed_a_module_into_at_least_one_process = false;

  // Loop through the multiboot sections.
  for (multiboot_tag *tag = next_multiboot_module_to_pass_to_process;
       tag->type != MULTIBOOT_TAG_TYPE_END; tag = NextMultibootTag(tag)) {
    // Found a multiboot module.
    if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
      multiboot_tag_module *module_tag = (multiboot_tag_module *)tag;
      size_t size = module_tag->mod_start - module_tag->mod_end;
      if (size == 0) {
        // Replace the tag so empty modules get skipped over later.
        tag->type = kLoadedMultibootTagType;
        continue;  // Skip over empty modules.
      }

      if (LoadElfProcess(module_tag->mod_start + VIRTUAL_MEMORY_OFFSET,
                         module_tag->mod_end + VIRTUAL_MEMORY_OFFSET,
                         module_tag->cmdline)) {
        tag->type = kLoadedMultibootTagType;
      } else {
        // This multiboot module can't be loaded, so count is as one that needs
        // to load later.
        multiboot_modules_to_pass_to_process++;
      }
    }
  }
}

void LoadNextMultibootModuleIntoProcess(Process *process,
                                        size_t &address_and_flags, size_t &size,
                                        char *name) {
  if (!HasRemainingUnloadedMultibootModules() ||
      !CanProcessRequestModule(process)) {
    // There are no more modules to load.
    size = 0;
    return;
  }

  // Loop over the multiboot tags to find either the end of the multiboot tags
  // or a module.
  multiboot_tag *tag = next_multiboot_module_to_pass_to_process;
  for (; tag->type != MULTIBOOT_TAG_TYPE_END &&
         tag->type != MULTIBOOT_TAG_TYPE_MODULE;
       tag = NextMultibootTag(tag)) {
  }

  if (tag == MULTIBOOT_TAG_TYPE_END) {
    // Reached the end earlier than expected.
    multiboot_modules_to_pass_to_process = 0;
    size = 0;
  } else {
    // Found a valid module to pass to a process.
    LoadMultibootModuleIntoProcess(process, (multiboot_tag_module *)tag,
                                   address_and_flags, size, name);

    // Jump to the next module so the subsequent call returns a new module.
    next_multiboot_module_to_pass_to_process = NextMultibootTag(tag);
    multiboot_modules_to_pass_to_process--;
  }

  if (!HasRemainingUnloadedMultibootModules()) {
    // There are no more modules to process, so the multiboot memory can be
    // released for other uses.
    DoneWithMultibootMemory();
  }
}

bool HasRemainingUnloadedMultibootModules() {
  return multiboot_modules_to_pass_to_process > 0;
}
