// Copyright 2021 Google LLC
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

#include "shared_memory_pool.h"

#include <stack>

#include "perception/memory.h"

using ::perception::kPageSize;
using ::perception::SharedMemory;

namespace {

std::stack<std::unique_ptr<PooledSharedMemory>> shared_memory_pool;

}

std::unique_ptr<PooledSharedMemory> GetSharedMemory() {
	if (shared_memory_pool.empty()) {
		auto pooled_shared_memory =
			std::make_unique<PooledSharedMemory>();
		pooled_shared_memory->shared_memory =
			SharedMemory::FromSize(kPageSize);
		(void)pooled_shared_memory->shared_memory->Join();
		return pooled_shared_memory;
	} else {
		auto pooled_shared_memory =
			std::move(shared_memory_pool.top());
		shared_memory_pool.pop();
		return pooled_shared_memory;
	}
}

void ReleaseSharedMemory(
	std::unique_ptr<PooledSharedMemory> pooled_shared_memory) {
	shared_memory_pool.push(std::move(pooled_shared_memory));
}