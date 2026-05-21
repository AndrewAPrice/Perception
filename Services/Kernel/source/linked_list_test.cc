// Copyright 2026 Google LLC
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

#include "linked_list.h"
#include "testing.h"
#include "object_pools.h"

struct Item {
  int val;
  LinkedListNode node;
};

TEST(LinkedListTest) {
  InitializeObjectPools();
  LinkedList<Item, &Item::node> list;

  ASSERT(list.IsEmpty(), true);

  Item i1 = {10, {}};
  Item i2 = {20, {}};
  Item i3 = {30, {}};

  list.AddBack(&i2);
  list.AddFront(&i1);
  list.AddBack(&i3);

  ASSERT(list.IsEmpty(), false);
  ASSERT(list.FirstItem(), &i1);
  ASSERT(list.LastItem(), &i3);

  // Iterate and verify order: 10 -> 20 -> 30
  int expected_vals[] = {10, 20, 30};
  int idx = 0;
  for (Item* it : list) {
    ASSERT(it->val, expected_vals[idx++]);
  }
  ASSERT(idx, 3);

  // Insert 15 before 20 => 10 -> 15 -> 20 -> 30
  Item i1_5 = {15, {}};
  list.InsertBefore(&i2, &i1_5);
  ASSERT(list.NextItem(&i1), &i1_5);
  ASSERT(list.NextItem(&i1_5), &i2);

  // Remove 15
  list.Remove(&i1_5);
  ASSERT(list.NextItem(&i1), &i2);

  // Pop front (10) and pop back (30)
  ASSERT(list.PopFront(), &i1);
  ASSERT(list.PopBack(), &i3);

  ASSERT(list.FirstItem(), &i2);
  ASSERT(list.LastItem(), &i2);

  // Clear the rest
  ASSERT(list.PopFront(), &i2);
  ASSERT(list.IsEmpty(), true);
}
