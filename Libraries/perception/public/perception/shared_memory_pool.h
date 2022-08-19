// Copyright 2022 Google LLC
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

#pragma once

#include <stack>

#include "perception/memory.h"
#include "perception/shared_memory.h"

namespace perception {

// Shared memory that can be put in a pool to be recycled.
struct PooledSharedMemory {
	std::unique_ptr<::perception::SharedMemory> shared_memory;
};

template<int shared_memory_size>
class SharedMemoryPool {
public:
	// Gets shared memory. Tries to recycle a previously released shared if
	// possible.
	std::unique_ptr<PooledSharedMemory> GetSharedMemory() {
		if (shared_memory_pool_.empty()) {
			// There's no shared memory we can recycle.
			auto pooled_shared_memory =
				std::make_unique<PooledSharedMemory>();
			pooled_shared_memory->shared_memory =
				SharedMemory::FromSize(shared_memory_size);
			(void)pooled_shared_memory->shared_memory->Join();
			return pooled_shared_memory;
		} else {
			// We can recycle previously released shared memory from the pool.
			auto pooled_shared_memory =
				std::move(shared_memory_pool_.top());
			shared_memory_pool_.pop();
			return pooled_shared_memory;
		}
	}

	// Returns the shared memory to the pool.
	void ReleaseSharedMemory(
		std::unique_ptr<PooledSharedMemory> shared_memory) {
		shared_memory_pool_.push(std::move(shared_memory));
	}

private:
	// Shared memory that is allocated but not used.
	std::stack<std::unique_ptr<PooledSharedMemory>> shared_memory_pool_;
};

}