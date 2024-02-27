#include "object_pools.h"

#include "interrupts.h"
#include "io.h"
#include "liballoc.h"
#include "messages.h"
#include "object_pool.h"
#include "process.h"
#include "service.h"
#include "shared_memory.h"
#include "thread.h"
#include "timer_event.h"
#include "virtual_allocator.h"

// A list of classes for which there are object pools for.
#define POOLED_CLASSES                                                       \
  FreeMemoryRange, Message, MessageToFireOnInterrupt, ProcessToNotifyOnExit, \
      ProcessToNotifyWhenServiceAppears, Service, SharedMemory,              \
      SharedMemoryInProcess, TimerEvent, Thread,                             \
      ThreadWaitingForSharedMemoryPage

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
void CleanUpObjectPools() { ObjectPoolHelper::CleanUpAllPools<POOLED_CLASSES>(); }
