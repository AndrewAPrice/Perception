#include "object_pools.h"

#include "liballoc.h"
#include "messages.h"
#include "process.h"
#include "service.h"
#include "shared_memory.h"
#include "timer_event.h"

struct ObjectPoolItem {
	struct ObjectPoolItem* next;
};

// Grabs an object from the pool, if it exists, or allocates a new object.
void* GrabOrAllocateObject(struct ObjectPoolItem** object_pool, size_t size) {
	if (*object_pool == NULL) {
		return malloc(size);
	} else {
		void* obj_to_return = *object_pool;
		*object_pool = (*object_pool)->next;
		return obj_to_return;
	}
}

// Returns an object to the pool.
void ReleaseObjectToPool(struct ObjectPoolItem** object_pool, void* item) {
	((struct ObjectPoolItem*)item)->next = *object_pool;
	*object_pool = ((struct ObjectPoolItem*)item);
}

// Frees all the objects in the pool
void FreeObjectsInPool(struct ObjectPoolItem** object_pool) {
	while (*object_pool != NULL) {
		struct ObjectPoolItem* next = (*object_pool)->next;
		free(*object_pool);
		*object_pool = next;
	}
}

#define OBJECT_POOL(Struct, CamelCase) \
	struct ObjectPoolItem* CamelCase##_pool = NULL; \
	struct Struct* Allocate##Struct() { \
		return (struct Struct*) GrabOrAllocateObject(&CamelCase##_pool,\
			sizeof (struct Struct)); \
	} \
	void Release##Struct(struct Struct* obj) { \
		ReleaseObjectToPool(&CamelCase##_pool, obj); \
	}

OBJECT_POOL(Message, message)
OBJECT_POOL(ProcessToNotifyOnExit, process_to_notify_on_exit)
OBJECT_POOL(ProcessToNotifyWhenServiceAppears, process_to_notify_when_service_appears)
OBJECT_POOL(Service, service)
OBJECT_POOL(SharedMemory, shared_memory)
OBJECT_POOL(SharedMemoryInProcess, shared_memory_in_process)
OBJECT_POOL(SharedMemoryPage, shared_memory_page)
OBJECT_POOL(TimerEvent, timer_event)

// Initialize the object pools.
void InitializeObjectPools() {
	message_pool = NULL;
	process_to_notify_on_exit_pool = NULL;
	process_to_notify_when_service_appears_pool = NULL;
	service_pool = NULL;
	shared_memory_pool = NULL;
	shared_memory_in_process_pool = NULL;
	shared_memory_page_pool = NULL;
	timer_event_pool = NULL;
}

// Clean up object pools to gain some memory back.
void CleanUpObjectPools() {
	FreeObjectsInPool(&message_pool);
	FreeObjectsInPool(&process_to_notify_on_exit_pool);
	FreeObjectsInPool(&process_to_notify_when_service_appears_pool);
	FreeObjectsInPool(&service_pool);	
	FreeObjectsInPool(&shared_memory_pool);
	FreeObjectsInPool(&shared_memory_in_process_pool);
	FreeObjectsInPool(&shared_memory_page_pool);
	FreeObjectsInPool(&timer_event_pool);
}
