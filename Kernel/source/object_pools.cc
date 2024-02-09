#include "object_pools.h"

#include "io.h"
#include "liballoc.h"
#include "messages.h"
#include "object_pool.h"
#include "process.h"
#include "service.h"
#include "shared_memory.h"
#include "timer_event.h"
#include "virtual_allocator.h"

// Initializer that can touch the private members of ObjectPool.
class ObjectPoolInitializer {
  public:
  static void Initialize() {
    ObjectPool<Message>::next_item_= nullptr;
    ObjectPool<ProcessToNotifyOnExit>::next_item_= nullptr;
    ObjectPool<Service>::next_item_= nullptr;
    ObjectPool<ProcessToNotifyWhenServiceAppears>::next_item_= nullptr;
    ObjectPool<SharedMemory>::next_item_= nullptr;
    ObjectPool<SharedMemoryInProcess>::next_item_ = nullptr;
    ObjectPool<TimerEvent>::next_item_ = nullptr;
    ObjectPool<ThreadWaitingForSharedMemoryPage>::next_item_ = nullptr;
    ObjectPool<FreeMemoryRange>::next_item_ = nullptr;
  }
};

// Initialize the object pools.
void InitializeObjectPools() {
  ObjectPoolInitializer::Initialize();
}

// Clean up object pools to gain some memory back.
void CleanUpObjectPools() {
  ObjectPool<Message>::FreeObjectsInPool();
  ObjectPool<ProcessToNotifyOnExit>::FreeObjectsInPool();
  ObjectPool<Service>::FreeObjectsInPool();
  ObjectPool<ProcessToNotifyWhenServiceAppears>::FreeObjectsInPool();
  ObjectPool<SharedMemory> ::FreeObjectsInPool();
  ObjectPool<SharedMemoryInProcess>::FreeObjectsInPool();
  ObjectPool<TimerEvent>::FreeObjectsInPool();
  ObjectPool<ThreadWaitingForSharedMemoryPage>::FreeObjectsInPool();
  ObjectPool<FreeMemoryRange>::FreeObjectsInPool();
}
