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
  AATreeNode* parent;

  // The child that is has a lower and higher value than this node.  If there
  // are multiple nodes with the same value, this field is only valid for the
  // first node.
  AATreeNode *left, *right;

  // Linked list of nodes of the same value.
  AATreeNode *previous, *next;
};

// An AA tree, which is a self balancing binary trree.
struct AATree {
  AATreeNode* root;
};

extern void InitializeAATree(AATree* tree);

extern void InsertNodeIntoAATree(
    AATree* tree, AATreeNode* node,
    size_t (*value_function)(AATreeNode* node));

extern void RemoveNodeFromAATree(
    AATree* tree, AATreeNode* node,
    size_t (*value_function)(AATreeNode* node));

extern AATreeNode* SearchForNodeLessThanOrEqualToValue(
    AATree* tree, size_t value,
    size_t (*value_function)(AATreeNode* node));

extern AATreeNode* SearchForNodeGreaterThanOrEqualToValue(
    AATree* tree, size_t value,
    size_t (*value_function)(AATreeNode* node));

extern AATreeNode* SearchForNodeEqualToValue(
    AATree* tree, size_t value,
    size_t (*value_function)(AATreeNode* node));

extern void PrintAATree(AATree* tree,
                        size_t (*value_function)(AATreeNode* node));

extern size_t CountNodesInAATree(AATree* tree);