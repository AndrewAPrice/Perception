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

  // Test duplicate entries.
  {
    TestAATree tree2;
    Node n1{100, {}};
    Node n2{100, {}};
    Node n3{100, {}};
    Node n4{50, {}};
    Node n5{150, {}};

    tree2.Insert(&n4);
    tree2.Insert(&n1);
    tree2.Insert(&n2);
    tree2.Insert(&n3);
    tree2.Insert(&n5);

    ASSERT(tree2.CountNodes(), (size_t)5);

    // Remove n3 (which is the front of the duplicate list for 100).
    tree2.Remove(&n3);
    ASSERT(tree2.CountNodes(), (size_t)4);
    ASSERT(tree2.SearchForItemEqualToValue(100) == &n2 ||
               tree2.SearchForItemEqualToValue(100) == &n1,
           true);

    // Remove n2.
    tree2.Remove(&n2);
    ASSERT(tree2.CountNodes(), (size_t)3);
    ASSERT(tree2.SearchForItemEqualToValue(100) == &n1, true);

    // Remove n1.
    tree2.Remove(&n1);
    ASSERT(tree2.CountNodes(), (size_t)2);
    ASSERT(tree2.SearchForItemEqualToValue(100) == nullptr, true);
  }

  // Test duplicate entries with level > 1.
  {
    TestAATree tree3;
    Node n10{10, {}};
    Node n20_a{20, {}};
    Node n30{30, {}};
    Node n20_b{20, {}};

    tree3.Insert(&n10);
    tree3.Insert(&n20_a);
    tree3.Insert(&n30);  // n20_a should be root at level 2.

    ASSERT(n20_a.node.level, 2);

    // Insert duplicate 20.
    tree3.Insert(&n20_b);

    // n20_b is now root, replacing n20_a.
    // Its level should be 2.
    ASSERT(n20_b.node.level, 2);
  }

  // Test traversal (IsEmpty, GetFirstItem, GetLastItem, GetNextItem,
  // GetPreviousItem) with unique and duplicate entries.
  {
    TestAATree tree4;
    ASSERT(tree4.IsEmpty(), true);
    ASSERT(tree4.GetFirstItem(), nullptr);
    ASSERT(tree4.GetLastItem(), nullptr);
    ASSERT(tree4.GetNextItem(nullptr), nullptr);
    ASSERT(tree4.GetPreviousItem(nullptr), nullptr);

    Node n10{10, {}};
    Node n20_a{20, {}};
    Node n20_b{20, {}};
    Node n30{30, {}};

    tree4.Insert(&n20_a);
    ASSERT(tree4.IsEmpty(), false);
    ASSERT(tree4.GetFirstItem(), &n20_a);
    ASSERT(tree4.GetLastItem(), &n20_a);
    ASSERT(tree4.GetNextItem(&n20_a), nullptr);
    ASSERT(tree4.GetPreviousItem(&n20_a), nullptr);
    ASSERT(tree4.GetNextItem(nullptr), nullptr);
    ASSERT(tree4.GetPreviousItem(nullptr), nullptr);

    tree4.Insert(&n10);
    tree4.Insert(&n30);

    // Now tree has 10, 20_a, 30.
    ASSERT(tree4.GetFirstItem(), &n10);
    ASSERT(tree4.GetLastItem(), &n30);
    ASSERT(tree4.GetNextItem(nullptr), nullptr);
    ASSERT(tree4.GetPreviousItem(nullptr), nullptr);

    // Forward traversal starting at FirstItem: 10 -> 20_a -> 30
    ASSERT(tree4.GetNextItem(tree4.GetFirstItem()), &n20_a);
    ASSERT(tree4.GetNextItem(&n20_a), &n30);
    ASSERT(tree4.GetNextItem(&n30), nullptr);

    // Backward traversal starting at LastItem: 30 -> 20_a -> 10
    ASSERT(tree4.GetPreviousItem(tree4.GetLastItem()), &n20_a);
    ASSERT(tree4.GetPreviousItem(&n20_a), &n10);
    ASSERT(tree4.GetPreviousItem(&n10), nullptr);

    // Insert duplicate 20.
    tree4.Insert(&n20_b);

    // Verify first and last items.
    ASSERT(tree4.GetFirstItem(), &n10);
    ASSERT(tree4.GetLastItem(), &n30);

    // Forward traversal order: 10 -> 20_b -> 20_a -> 30
    ASSERT(tree4.GetNextItem(tree4.GetFirstItem()), &n20_b);
    ASSERT(tree4.GetNextItem(&n20_b), &n20_a);
    ASSERT(tree4.GetNextItem(&n20_a), &n30);
    ASSERT(tree4.GetNextItem(&n30), nullptr);

    // Backward traversal order: 30 -> 20_a -> 20_b -> 10
    ASSERT(tree4.GetPreviousItem(tree4.GetLastItem()), &n20_a);
    ASSERT(tree4.GetPreviousItem(&n20_a), &n20_b);
    ASSERT(tree4.GetPreviousItem(&n20_b), &n10);
    ASSERT(tree4.GetPreviousItem(&n10), nullptr);
  }
}
