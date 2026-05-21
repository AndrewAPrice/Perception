// Copyright 2023 Google LLC
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

#include "aa_tree.h"

#include <stdlib.h>

#include "object_pools.h"
#include "testing.h"

struct Node {
  size_t value;
  AATreeNode node;
};

using TestAATree = AATree<Node, &Node::node, &Node::value>;

void VerifyBalancedTreeOf0To19(TestAATree* aa_tree) {
  ASSERT(aa_tree->CountNodes(), (size_t)20);
  // AATree doesn't expose root_ directly in a public way, but we can verify
  // via search correctness.
}

TEST(AATreeTest) {
  InitializeObjectPools();
  TestAATree aa_tree;

  Node** nodes = (Node**)malloc(sizeof(Node*) * 20);

  // Create some nodes.
  for (int start = 0; start < 5; start++) {
    for (int i = start; i < 20; i += 5) {
      Node* node = (Node*)malloc(sizeof(Node));
      node->value = i;
      nodes[i] = node;

      aa_tree.Insert(node);
    }
  }

  VerifyBalancedTreeOf0To19(&aa_tree);

  // Remove some nodes.
  aa_tree.Remove(nodes[0]);
  aa_tree.Remove(nodes[1]);
  aa_tree.Remove(nodes[2]);
  aa_tree.Remove(nodes[11]);
  aa_tree.Remove(nodes[13]);
  aa_tree.Remove(nodes[15]);

  ASSERT(aa_tree.CountNodes(), (size_t)14);

  ASSERT(aa_tree.SearchForItemLessThanOrEqualToValue(1) == nullptr, true);
  ASSERT(aa_tree.SearchForItemLessThanOrEqualToValue(3) == nodes[3], true);
  ASSERT(aa_tree.SearchForItemLessThanOrEqualToValue(11) == nodes[10], true);
  ASSERT(aa_tree.SearchForItemLessThanOrEqualToValue(99) == nodes[19], true);

  ASSERT(aa_tree.SearchForItemGreaterThanOrEqualToValue(20) == nullptr, true);
  ASSERT(aa_tree.SearchForItemGreaterThanOrEqualToValue(19) == nodes[19], true);
  ASSERT(aa_tree.SearchForItemGreaterThanOrEqualToValue(1) == nodes[3], true);
  ASSERT(aa_tree.SearchForItemGreaterThanOrEqualToValue(4) == nodes[4], true);
  ASSERT(aa_tree.SearchForItemGreaterThanOrEqualToValue(15) == nodes[16], true);

  ASSERT(aa_tree.SearchForItemEqualToValue(0) == nullptr, true);
  ASSERT(aa_tree.SearchForItemEqualToValue(20) == nullptr, true);
  ASSERT(aa_tree.SearchForItemEqualToValue(11) == nullptr, true);
  ASSERT(aa_tree.SearchForItemEqualToValue(10) == nodes[10], true);

  for (int i = 0; i < 20; i++) {
    free(nodes[i]);
  }
  free(nodes);
}
