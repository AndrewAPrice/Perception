#include "../../../third_party/multiboot2.h"
#include "framebuffer.h"
#include "interrupts.h"
#include "io.h"
#include "multiboot_modules.h"
#include "object_pools.h"
#include "physical_allocator.h"
#include "process.h"
#include "profiling.h"
#include "scheduler.h"
#include "service.h"
#include "shared_memory.h"
#include "syscall.h"
#include "text_terminal.h"
#include "thread.h"
#include "timer.h"
#include "tss.h"
#include "virtual_allocator.h"

#ifndef __TEST__
extern "C" void kmain() {
  InitializePrinter();

  // Make sure we were booted with a multiboot2 bootloader - we need this
  // because we depend on GRUB for providing us with some initialization
  // information.
  if (MultibootInfo.magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
    print << "Not booted with a multiboot2 bootloader!";
    asm("hlt");
  }

  InitializePhysicalAllocator();
  InitializeObjectPools();
  InitializeVirtualAllocator();

  InitializeTss();
  InitializeInterrupts();
  InitializeSystemCalls();

  InitializeProcesses();
  InitializeThreads();
  InitializeServices();
  InitializeSharedMemory();

  InitializeScheduler();
  InitializeTimer();
  InitializeProfiling();

  // Loads the multiboot modules, then frees the memory used by them.
  LoadMultibootModules();
  MaybeLoadFramebuffer();
  if (!HasRemainingUnloadedMultibootModules())
    DoneWithMultibootMemory();

  asm("sti");
  for (;;) {
    // This needs to be in a loop because the scheduler returns here when there
    // are no awake threads scheduled.
    asm("hlt");
  }
}
#endif
