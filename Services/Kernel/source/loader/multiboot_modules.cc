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

#include "loader/multiboot_modules.h"

#include "../../../third_party/multiboot2.h"
#include "containers/spinlock.h"
#include "loader/elf_loader.h"
#include "common/kernel_string.h"
#include "memory/memory.h"
#include "memory/physical_allocator.h"
#include "memory/virtual_address_space.h"
#include "memory/virtual_allocator.h"
#include "output/text_terminal.h"
#include "processes/process.h"

namespace loader {

using memory::CopyKernelMemoryIntoProcess;
using memory::DoneWithMultibootMemory;
using memory::kVirtualMemoryOffset;
using memory::PagesThatContainBytes;
using output::print;
using processes::Process;

#ifndef TEST
namespace {

// A tag type to set for multiboot modules that have been loaded, so they are
// skipped later when a process wants to iterate over the unloaded multiboot
// modules.
constexpr uint32 kLoadedMultibootTagType = 0xFFFFFFFF;

// Spinlock protecting the multiboot module iteration state below, which can be
// requested concurrently by processes running on different cores.
containers::InterruptSafeSpinlock g_multiboot_spinlock;

// The next multiboot module to pass to a process.
multiboot_tag* g_next_multiboot_module_to_pass_to_process;

// The number of multiboot modules to pass to a process.
size_t g_multiboot_modules_to_pass_to_process;

// The end of the multiboot tags.
size_t g_multiboot_end;

// Whether a module has been passed into at least one process.
bool g_has_passed_a_module_into_at_least_one_process = false;

// The pid of the process that modules can be passed to. This is so only the
// same process can keep requesting modules.
size_t g_pid_of_process_that_modules_can_be_passed_to;

// Returns whether there are modules left to pass to a process. Must be called
// with 'g_multiboot_spinlock' held.
bool HasRemainingUnloadedMultibootModulesLocked() {
  return g_multiboot_modules_to_pass_to_process > 0;
}

// Returns whether a process can be passed a module.
bool CanProcessRequestModule(Process* process) {
  if (g_has_passed_a_module_into_at_least_one_process) {
    if (g_pid_of_process_that_modules_can_be_passed_to != process->pid) {
      // This isn't the same process that modules were previously passed ot.
      return false;
    }
  } else {
    // This is the first process that has asked for a module, so remember this
    // process so only the same one can keep requesting modules.
    g_has_passed_a_module_into_at_least_one_process = true;
    g_pid_of_process_that_modules_can_be_passed_to = process->pid;
  }

  return true;
}

// Returns the following multiboot tag.
multiboot_tag* NextMultibootTag(multiboot_tag* tag) {
  if (tag == nullptr || tag->size < 8) return nullptr;
  return (multiboot_tag*)((size_t)tag + (size_t)((tag->size + 7) & ~7));
}

// Loads a multiboot module into a process. `process` and `tag` are inputs. All
// other parameters are outputs.
void LoadMultibootModuleIntoProcess(Process* process, multiboot_tag_module* tag,
                                    size_t& address_and_flags, size_t& size,
                                    char* name) {
  // Parse the command line into the name.
  char* cmdline = tag->cmdline;
  size_t name_length = 0;
  bool is_driver = false;
  bool can_create_processes = false;
  bool can_set_focus = false;

  if (!ParseMultibootModuleName(cmdline, name_length, is_driver,
                                can_create_processes, can_set_focus)) {
    // Skip this module because of an invalid name. This shouldn't have happened
    // because LoadElfProcess should have returned true in this case.
    size = 0;
    return;
  }

  common::CopyString(cmdline, kModuleNameLength, name_length, name);

  // Calculate the size and allocate the virtual memory to copy this multiboot
  // module into. The bootloader supplies both ends, so a malformed module could
  // otherwise underflow the subtraction into a near-2^64 size.
  if (tag->mod_end < tag->mod_start) {
    print << "Multiboot module " << name << " has an invalid extent.\n";
    size = 0;
    return;
  }

  size = tag->mod_end - tag->mod_start;
  size_t pages = PagesThatContainBytes(size);
  address_and_flags = process->virtual_address_space.AllocatePages(pages);
  if (address_and_flags == kOutOfMemory) {
    print << "Out of memory, can't pass module " << name << " to "
          << process->name << ".\n";
    size = 0;
    return;
  }

  // Copy the multiboot module into the process's virtual memory.
  if (!CopyKernelMemoryIntoProcess(tag->mod_start + kVirtualMemoryOffset,
                                   address_and_flags, address_and_flags + size,
                                   process)) {
    print << "Couldn't copy module " << name << " into " << process->name
          << ".\n";
    process->virtual_address_space.ReleasePages(address_and_flags, pages);
    size = 0;
    return;
  }

  // Attach the flags into the start address.
  if (is_driver) address_and_flags |= 1;
  if (can_create_processes) address_and_flags |= 2;
  if (can_set_focus) address_and_flags |= 4;
}
}  // namespace
#endif // TEST

bool ParseMultibootModuleName(char*& name, size_t& name_length, bool& is_driver,
                              bool& can_create_processes, bool& can_set_focus) {
  name_length = strlen(name);
  if (name_length == 0) return false;

  // If the command line begins with a path (starts with '/'), skip past the
  // initial file path and space to reach the attribute flags.
  if (*name == '/') {
    while (name_length > 0 && *name != ' ') {
      if (*name == '\\' && name_length > 1 && *(name + 1) == ' ') {
        name += 2;
        name_length -= 2;
        continue;
      }
      name++;
      name_length--;
    }
    if (name_length > 0 && *name == ' ') {
      name++;
      name_length--;
    }
  }

  while (true) {
    if (name_length == 0) return false;  // Out of letters.

    if (*name == ' ') {
      // Reached a space.

      // Jumped over the space.
      name_length--;
      name++;

      // A valid name if we still have at least 1 character.
      return name_length >= 1;
    }

    // Switch over this permission.
    switch (*name) {
      case 'd':
        is_driver = true;
        break;
      case 'l':
        can_create_processes = true;
        break;
      case 'f':
        can_set_focus = true;
        break;
      case '-':
        break;
      default:
        print << "Unknown attribute '" << *name << "'.";
        return false;
    }

    // Jump over this character.
    name++;
    name_length--;
  }
}

#ifndef TEST
void LoadMultibootModules() {
  // Now in higher half memory, so kVirtualMemoryOffset must be added.
  multiboot_info* higher_half_multiboot_info =
      (multiboot_info*)((size_t)&MultibootInfo + kVirtualMemoryOffset);

  size_t multiboot_address =
      higher_half_multiboot_info->addr + kVirtualMemoryOffset;
  g_multiboot_end = multiboot_address + *(uint32*)multiboot_address;

  g_multiboot_modules_to_pass_to_process = 0;
  g_next_multiboot_module_to_pass_to_process =
      (multiboot_tag*)(size_t)(higher_half_multiboot_info->addr + 8 +
                               kVirtualMemoryOffset);
  g_has_passed_a_module_into_at_least_one_process = false;

  // Loop through the multiboot sections.
  for (multiboot_tag* tag = g_next_multiboot_module_to_pass_to_process;
       tag != nullptr && (size_t)tag + sizeof(multiboot_tag) <= g_multiboot_end &&
       tag->type != MULTIBOOT_TAG_TYPE_END;
       tag = NextMultibootTag(tag)) {
    // Found a multiboot module.
    if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
      multiboot_tag_module* module_tag = (multiboot_tag_module*)tag;
      size_t size = module_tag->mod_end - module_tag->mod_start;
      if (size == 0) {
        // Replace the tag so empty modules get skipped over later.
        tag->type = kLoadedMultibootTagType;
        continue;  // Skip over empty modules.
      }

      if (LoadElfProcess(module_tag->mod_start + kVirtualMemoryOffset,
                         module_tag->mod_end + kVirtualMemoryOffset,
                         module_tag->cmdline)) {
        tag->type = kLoadedMultibootTagType;
      } else {
        // This multiboot module can't be loaded, so count it as one that needs
        // to be loaded later.
        g_multiboot_modules_to_pass_to_process++;
      }
    }
  }
}

void LoadNextMultibootModuleIntoProcess(Process* process,
                                        size_t& address_and_flags, size_t& size,
                                        char* name) {
  // Cleared up front, because the early returns below leave the name unset and
  // the caller returns this buffer straight to userspace.
  memset(name, 0, kModuleNameLength);

  multiboot_tag_module* module_to_load = nullptr;
  bool done_with_multiboot_memory = false;
  {
    containers::InterruptSafeSpinlockGuard guard(g_multiboot_spinlock);

    if (!HasRemainingUnloadedMultibootModulesLocked() ||
        !CanProcessRequestModule(process)) {
      // There are no more modules to load.
      size = 0;
      return;
    }

    // Loop over the multiboot tags to find either the end of the multiboot tags
    // or a module.
    multiboot_tag* tag = g_next_multiboot_module_to_pass_to_process;
    for (; tag != nullptr &&
           (size_t)tag + sizeof(multiboot_tag) <= g_multiboot_end &&
           tag->type != MULTIBOOT_TAG_TYPE_END &&
           tag->type != MULTIBOOT_TAG_TYPE_MODULE;
         tag = NextMultibootTag(tag)) {
    }

    if (tag == nullptr ||
        (size_t)tag + sizeof(multiboot_tag) > g_multiboot_end ||
        tag->type == MULTIBOOT_TAG_TYPE_END) {
      // Reached the end earlier than expected.
      g_multiboot_modules_to_pass_to_process = 0;
      size = 0;
    } else {
      // Found a valid module to pass to a process.
      module_to_load = (multiboot_tag_module*)tag;

      // Jump to the next module so the subsequent call returns a new module.
      g_next_multiboot_module_to_pass_to_process = NextMultibootTag(tag);
      g_multiboot_modules_to_pass_to_process--;
    }

    done_with_multiboot_memory = !HasRemainingUnloadedMultibootModulesLocked();
  }

  if (module_to_load != nullptr) {
    LoadMultibootModuleIntoProcess(process, module_to_load, address_and_flags,
                                   size, name);
  }

  if (done_with_multiboot_memory) {
    // There are no more modules to process, so the multiboot memory can be
    // released for other uses.
    DoneWithMultibootMemory();
  }
}

bool HasRemainingUnloadedMultibootModules() {
  containers::InterruptSafeSpinlockGuard guard(g_multiboot_spinlock);
  return HasRemainingUnloadedMultibootModulesLocked();
}

#endif  // TEST

}  // namespace loader
