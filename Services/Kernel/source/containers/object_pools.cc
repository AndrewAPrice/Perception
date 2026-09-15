#include "containers/object_pools.h"

#include "memory/heap_allocator.h"
#include "interrupts/interrupts.h"
#include "hardware/io.h"
#include "ipc/messages.h"
#include "containers/object_pool.h"
#include "processes/process.h"
#include "ipc/rpc.h"
#include "ipc/service.h"
#include "containers/set.h"
#include "ipc/shared_memory.h"
#include "ipc/shared_memory_event.h"
#include "scheduling/thread.h"
#include "scheduling/timer_event.h"
#include "memory/virtual_address_space.h"

namespace containers {

// A list of classes for which there are object pools for.
#define POOLED_CLASSES                                                      \
  memory::VirtualAddressSpace::FreeMemoryRange, ipc::Message,               \
      interrupts::MessageToFireOnInterrupt,                                 \
      processes::ProcessToNotifyOnExit,                                    \
      ipc::ProcessToNotifyWhenServiceAppears,                               \
      ipc::ProcessToNotifyWhenServiceDisappears, SetNode, ipc::Service,     \
      ipc::SharedMemory, ipc::SharedMemoryInProcess,                         \
      scheduling::TimerEvent, scheduling::Thread,                           \
      ipc::ThreadWaitingForSharedMemoryPage, ipc::SharedMemoryEvent,        \
      ipc::RPC

// Initializer that can touch the private members of ObjectPool.
class ObjectPoolHelper {
 public:
  // Initializes all object pools for the types passed in as template arguments.
  template <typename... T>
  static void InitializeAllPools() {
    ((void)InitializeObjectPool<T>(), ...);
  }

  // Cleans up all object pools for the types passed in as template arguments.
  template <typename... T>
  static void CleanUpAllPools() {
    ((void)CleanUpObjectPool<T>(), ...);
  }

 private:
  // Initializes the object pool for the type passed in as template arguments.
  template <typename T>
  static void InitializeObjectPool() {
    ObjectPool<T>::next_item_ = nullptr;
    for (size_t c = 0; c < ::hardware::kMaxCores; c++) {
      ObjectPool<T>::local_cache_[c] = nullptr;
      ObjectPool<T>::local_count_[c] = 0;
    }
  }

  // Cleans up the object pool for the type passed in as a template argument.
  template <typename T>
  static void CleanUpObjectPool() {
    ObjectPool<T>::FreeObjectsInPool();
  }
};

// Initialize the object pools.
void InitializeObjectPools() {
  ObjectPoolHelper::InitializeAllPools<POOLED_CLASSES>();
}

// Clean up object pools to gain some memory back.
void CleanUpObjectPools() {
  ObjectPoolHelper::CleanUpAllPools<POOLED_CLASSES>();
}

}  // namespace containers
