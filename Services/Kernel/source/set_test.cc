// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.pragma.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "set.h"
#include "testing.h"
#include "object_pools.h"

TEST(SetTest) {
  InitializeObjectPools();
  Set<size_t> my_set;

  ASSERT(my_set.Contains(100), false);

  // Insert elements
  my_set.Insert(100);
  my_set.Insert(200);
  my_set.Insert(300);

  // Insert duplicate
  my_set.Insert(200);

  // Verify presence
  ASSERT(my_set.Contains(100), true);
  ASSERT(my_set.Contains(200), true);
  ASSERT(my_set.Contains(300), true);
  ASSERT(my_set.Contains(400), false);

  // Remove element
  my_set.Remove(200);
  ASSERT(my_set.Contains(200), false);
  ASSERT(my_set.Contains(100), true);
}
