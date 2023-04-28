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

#pragma once

#include "types.h"

// A node in an AA-tree.
struct AATreeNode {
  // This node's level. This has to do with the tree structure and is not
  // related to the node's value.
  uint8 level;

  // The parent in the tree structure. If there are multiple nodes with the same
  // value, this field is only valid for the first node.
  struct AATreeNode* parent;

  // The child that is has a lower and higher value than this node.  If there
  // are multiple nodes with the same value, this field is only valid for the
  // first node.
  struct AATreeNode *left, *right;

  // Linked list of nodes of the same value.
  struct AATreeNode *previous, *next;
};

// An AA tree, which is a self balancing binary trree.
struct AATree {
  struct AATreeNode* root;
};

extern void InitializeAATree(struct AATree* tree);

extern void InsertNodeIntoAATree(
    struct AATree* tree, struct AATreeNode* node,
    size_t (*value_function)(struct AATreeNode* node));

extern void RemoveNodeFromAATree(
    struct AATree* tree, struct AATreeNode* node,
    size_t (*value_function)(struct AATreeNode* node));

extern struct AATreeNode* SearchForNodeLessThanOrEqualToValue(
    struct AATree* tree, size_t value,
    size_t (*value_function)(struct AATreeNode* node));

extern struct AATreeNode* SearchForNodeGreaterThanOrEqualToValue(
    struct AATree* tree, size_t value,
    size_t (*value_function)(struct AATreeNode* node));

extern struct AATreeNode* SearchForNodeEqualToValue(
    struct AATree* tree, size_t value,
    size_t (*value_function)(struct AATreeNode* node));

extern void PrintAATree(struct AATree* tree,
                        size_t (*value_function)(struct AATreeNode* node));

extern size_t CountNodesInAATree(struct AATree* tree);