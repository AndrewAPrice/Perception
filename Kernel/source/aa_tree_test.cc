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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

struct Node {
  int value;
  struct AATreeNode node;
};

struct Node *NodeFromAATreeNode(struct AATreeNode *node) {
  size_t node_offset = (size_t) & ((struct Node *)0)->node;
  return (struct Node *)((size_t)node - node_offset);
}

// Returns the start address from a pointer to a `node_by_address` field.
size_t ValueOfAATreeNode(struct AATreeNode *node) {
  return NodeFromAATreeNode(node)->value;
}

size_t ValueOfNode(struct Node *node) { return node->value; }

void AssertNodeNotNullAndHasValue(struct AATreeNode *node, int value) {
  assert(node != nullptr);
  assert(ValueOfAATreeNode(node) == value);
}

int CalculateNodeHeight(struct AATreeNode *aa_node) {
  if (aa_node == nullptr) return 0;

  int left = CalculateNodeHeight(aa_node->left);
  int right = CalculateNodeHeight(aa_node->right);
  return (left > right ? left : right) + 1;
}

int CalculateMaxTree(struct AATree *aa_tree) {
  assert(aa_tree != nullptr);
  return CalculateNodeHeight(aa_tree->root);
}

void VerifyBalancedTreeOf0To19(struct AATree *aa_tree) {
  assert(CountNodesInAATree(aa_tree) == 20);
  AssertNodeNotNullAndHasValue(aa_tree->root, 10);
  AssertNodeNotNullAndHasValue(aa_tree->root->left, 5);
  AssertNodeNotNullAndHasValue(aa_tree->root->left->left, 1);
  AssertNodeNotNullAndHasValue(aa_tree->root->left->left->left, 0);
  AssertNodeNotNullAndHasValue(aa_tree->root->left->left->right, 3);
  AssertNodeNotNullAndHasValue(aa_tree->root->left->left->right->left, 2);
  AssertNodeNotNullAndHasValue(aa_tree->root->right, 15);
  AssertNodeNotNullAndHasValue(aa_tree->root->right->left, 12);
  AssertNodeNotNullAndHasValue(aa_tree->root->right->left->left, 11);
  AssertNodeNotNullAndHasValue(aa_tree->root->right->left->right, 13);
  AssertNodeNotNullAndHasValue(aa_tree->root->right->left->right->right, 14);
  AssertNodeNotNullAndHasValue(aa_tree->root->right->right, 17);
  AssertNodeNotNullAndHasValue(aa_tree->root->right->right->left, 16);
  AssertNodeNotNullAndHasValue(aa_tree->root->right->right->right, 18);
  AssertNodeNotNullAndHasValue(aa_tree->root->right->right->right->right, 19);
  assert(CalculateMaxTree(aa_tree) == 5);
}

void VerifyRebalancedTree(struct AATree *aa_tree) {
  assert(CountNodesInAATree(aa_tree) == 14);
  AssertNodeNotNullAndHasValue(aa_tree->root, 7);
  AssertNodeNotNullAndHasValue(aa_tree->root->left, 5);
  AssertNodeNotNullAndHasValue(aa_tree->root->left->left, 3);
  AssertNodeNotNullAndHasValue(aa_tree->root->left->left->right, 4);
  AssertNodeNotNullAndHasValue(aa_tree->root->left->right, 6);
  AssertNodeNotNullAndHasValue(aa_tree->root->right, 10);
  AssertNodeNotNullAndHasValue(aa_tree->root->right->left, 8);
  AssertNodeNotNullAndHasValue(aa_tree->root->right->left->right, 9);
  AssertNodeNotNullAndHasValue(aa_tree->root->right->right, 12);
  AssertNodeNotNullAndHasValue(aa_tree->root->right->right->right, 17);
  AssertNodeNotNullAndHasValue(aa_tree->root->right->right->right->left, 14);
  AssertNodeNotNullAndHasValue(aa_tree->root->right->right->right->left->right,
                               16);
  AssertNodeNotNullAndHasValue(aa_tree->root->right->right->right->right, 18);
  AssertNodeNotNullAndHasValue(aa_tree->root->right->right->right->right->right,
                               19);
  assert(CalculateMaxTree(aa_tree) == 6);
}

int main() {
  struct AATree *aa_tree = malloc(sizeof(struct AATree));
  InitializeAATree(aa_tree);

  struct Node **nodes = malloc(sizeof(size_t) * 20);

  // Create some nodes.
  for (int start = 0; start < 5; start++) {
    for (int i = start; i < 20; i += 5) {
      struct Node *node = malloc(sizeof(struct Node));
      node->value = i;
      nodes[i] = node;

      InsertNodeIntoAATree(aa_tree, &node->node, ValueOfAATreeNode);
    }
  }

  VerifyBalancedTreeOf0To19(aa_tree);

  // Remove some nodes.
  RemoveNodeFromAATree(aa_tree, &nodes[0]->node, ValueOfAATreeNode);
  RemoveNodeFromAATree(aa_tree, &nodes[1]->node, ValueOfAATreeNode);
  RemoveNodeFromAATree(aa_tree, &nodes[2]->node, ValueOfAATreeNode);
  RemoveNodeFromAATree(aa_tree, &nodes[11]->node, ValueOfAATreeNode);
  RemoveNodeFromAATree(aa_tree, &nodes[13]->node, ValueOfAATreeNode);
  RemoveNodeFromAATree(aa_tree, &nodes[15]->node, ValueOfAATreeNode);

  VerifyRebalancedTree(aa_tree);

  assert(SearchForNodeLessThanOrEqualToValue(aa_tree, 1, ValueOfAATreeNode) ==
         nullptr);
  assert(NodeFromAATreeNode(SearchForNodeLessThanOrEqualToValue(
             aa_tree, 3, ValueOfAATreeNode)) == nodes[3]);
  assert(NodeFromAATreeNode(SearchForNodeLessThanOrEqualToValue(
             aa_tree, 11, ValueOfAATreeNode)) == nodes[10]);
  assert(NodeFromAATreeNode(SearchForNodeLessThanOrEqualToValue(
             aa_tree, 99, ValueOfAATreeNode)) == nodes[19]);

  assert(SearchForNodeGreaterThanOrEqualToValue(aa_tree, 20,
                                                ValueOfAATreeNode) == nullptr);
  assert(NodeFromAATreeNode(SearchForNodeGreaterThanOrEqualToValue(
             aa_tree, 19, ValueOfAATreeNode)) == nodes[19]);
  assert(NodeFromAATreeNode(SearchForNodeGreaterThanOrEqualToValue(
             aa_tree, 1, ValueOfAATreeNode)) == nodes[3]);
  assert(NodeFromAATreeNode(SearchForNodeGreaterThanOrEqualToValue(
             aa_tree, 4, ValueOfAATreeNode)) == nodes[4]);
  assert(NodeFromAATreeNode(SearchForNodeGreaterThanOrEqualToValue(
             aa_tree, 15, ValueOfAATreeNode)) == nodes[16]);

  assert(SearchForNodeEqualToValue(aa_tree, 0, ValueOfAATreeNode) == nullptr);
  assert(SearchForNodeEqualToValue(aa_tree, 20, ValueOfAATreeNode) == nullptr);
  assert(SearchForNodeEqualToValue(aa_tree, 11, ValueOfAATreeNode) == nullptr);
  assert(NodeFromAATreeNode(SearchForNodeEqualToValue(
             aa_tree, 10, ValueOfAATreeNode)) == nodes[10]);

  for (int i = 0; i < 20; i++) {
    free(nodes[i]);
  }
  free(nodes);
  free(aa_tree);

  return 0;
}
