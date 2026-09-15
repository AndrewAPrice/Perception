#pragma once

namespace containers {

// Initialize the object pools.
void InitializeObjectPools();

// Clean up object pools to gain some memory back.
void CleanUpObjectPools();

}  // namespace containers

