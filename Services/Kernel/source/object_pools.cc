#include "object_pools.h"

#include "heap_allocator.h"
#include "interrupts.h"
#include "io.h"
#include "messages.h"
#include "object_pool.h"
#include "process.h"
#include "rpc.h"
#include "service.h"
#include "set.h"
#include "shared_memory.h"
#include "shared_memory_event.h"
#include "thread.h"
#include "timer_event.h"
#include "virtual_address_space.h"

// A list of classes for which there are object pools for.
#define POOLED_CLASSES                                                      \
  VirtualAddressSpace::FreeMemoryRange, Message, MessageToFireOnInterrupt,  \
      ProcessToNotifyOnExit, ProcessToNotifyWhenServiceAppears,             \
      ProcessToNotifyWhenServiceDisappears, SetNode, Service, SharedMemory, \
      SharedMemoryInProcess, TimerEvent, Thread,                            \
      ThreadWaitingForSharedMemoryPage, SharedMemoryEvent, RPC

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

template <>
void ObjectPool<VirtualAddressSpace::FreeMemoryRange>::FreeObjectsInPool() {
  ObjectPoolItem* prev = nullptr;
  ObjectPoolItem* curr = next_item_;
  while (curr != nullptr) {
    auto* fmr = reinterpret_cast<VirtualAddressSpace::FreeMemoryRange*>(curr);
    if (fmr->is_static) {
      prev = curr;
      curr = curr->next;
    } else {
      ObjectPoolItem* next = curr->next;
      if (prev == nullptr) {
        next_item_ = next;
      } else {
        prev->next = next;
      }
      free(curr);
      curr = next;
    }
  }
}
