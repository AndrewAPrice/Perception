#ifndef TEST
#include "../../../../third_party/multiboot2.h"
#include "containers/object_pools.h"
#include "diagnostics/profiling.h"
#include "hardware/acpi.h"
#include "hardware/fpu.h"
#include "hardware/smp.h"
#include "interrupts/interrupts.h"
#include "ipc/service.h"
#include "ipc/shared_memory.h"
#include "loader/multiboot_modules.h"
#include "memory/physical_allocator.h"
#include "memory/virtual_allocator.h"
#include "output/framebuffer.h"
#include "output/text_terminal.h"
#include "processes/process.h"
#include "scheduling/cpu_core.h"
#include "scheduling/scheduler.h"
#include "scheduling/thread.h"
#include "scheduling/timer.h"
#include "syscall/syscall.h"

extern "C" void kmain() {
  output::InitializePrinter();
  // Make sure the system was booted with a multiboot2 bootloader - this is
  // needed because GRUB provides some initialization
  // information.
  if (MultibootInfo.magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
    output::print << "Not booted with a multiboot2 bootloader!";
    asm volatile("cli");
    for (;;) asm volatile("hlt");
  }

  memory::InitializePhysicalAllocator();
  containers::InitializeObjectPools();
  scheduling::InitializeCpuCores();
  memory::InitializeVirtualAllocator();

  interrupts::InitializeInterrupts();
  syscall::InitializeSystemCalls();
  hardware::InitializeFpu();

  processes::InitializeProcesses();
  scheduling::InitializeThreads();
  ipc::InitializeServices();
  ipc::InitializeSharedMemory();

  hardware::InitializeAcpi();
  scheduling::InitializeScheduler();
  scheduling::InitializeTimer();
  diagnostics::InitializeProfiling();

  // Loads the multiboot modules, then frees the memory used by them.
  loader::LoadMultibootModules();
  output::MaybeLoadFramebuffer();
  hardware::InitializeSmp();
  if (!loader::HasRemainingUnloadedMultibootModules())
    memory::DoneWithMultibootMemory();

  asm("sti");
  for (;;) {
    // This needs to be in a loop because the scheduler returns here when there
    // are no awake threads scheduled.
    asm("hlt");
  }
}

#endif // TEST
