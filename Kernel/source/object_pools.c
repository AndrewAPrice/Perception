#include "object_pools.h"

#include "messages.h"
#include "liballoc.h"

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

// Messages:
struct ObjectPoolItem* message_pool = NULL;

// Allocate an message.
struct Message* AllocateMessage() {
	return (struct Message*) GrabOrAllocateObject(&message_pool, sizeof (struct Message));
}

// Release an message.
void ReleaseMessage(struct Message* message) {
	ReleaseObjectToPool(&message_pool, message);
}

// Clean up object pools to gain some memory back.
void CleanUpObjectPools() {
	FreeObjectsInPool(&message_pool);
}