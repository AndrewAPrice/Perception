// Copyright 2024 Google LLC
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

#include "aa_tree.h"
#include "object_pool.h"

// A set of values.
template <class T>
class Set {
 public:
  Set() { InitializeAATree(&tree_); }

  ~Set() {
    while (first_node_ != nullptr) {
      Node* next = first_node_->next;
      ObjectPool<Node>::Release(first_node_);
      first_node_ = next;
    }
  }

  // Inserts a value into the set. Does nothing if the value is already
  // inserted.
  void Insert(T value) {
    AATreeNode* aa_node = SearchForNodeEqualToValue(
        &tree_, static_cast<size_t>(value), Set<T>::ValueFunction);
    if (aa_node == nullptr) return;

    Node* node = ObjectPool<Node>::Allocate();
    node->value = value;
    node->next = first_node_;
    node->previous = nullptr;
    if (first_node_ != nullptr) first_node_->previous = node;
    InsertNodeIntoAATree(&tree_, &node->node, Set<T>::ValueFunction);
  }

  // Removes a value from the set. Does nothing if the value is not in the set.
  void Erase(T value) {
    AATreeNode* aa_node = SearchForNodeEqualToValue(
        &tree_, static_cast<size_t>(value), Set<T>::ValueFunction);
    if (aa_node == nullptr) return;

    Node* node = NodeFromAATreeNode(aa_node);
    if (node->previous == nullptr) {
      first_node_ = node->next;
    } else {
      node->previous->next = node->next;
    }
    if (node->next != nullptr) node->next->previous = node->previous;

    RemoveNodeFromAATree(&tree_, aa_node, Set<T>::ValueFunction);
    ObjectPool<Node>::Release(node);
  }

  // Returns if a value is in a set.
  bool Contains(T value) {
    return SearchForNodeEqualToValue(&tree_, static_cast<size_t>(value),
                                     Set<T>::ValueFunction) != nullptr;
  }

 private:
  // A node in the set.
  struct Node {
    // Value of this node.
    T value;

    // This node in the AATree.
    AATreeNode node;

    // The next node.
    Node* next;

    // The previous node.
    Node* previous;
  };

  // The root of the AATree.
  AATree tree_;

  // Linked list of nodes.
  Node* first_node_;

  // Returns the outer Node from an AATreeNode.
  static Node* NodeFromAATreeNode(AATreeNode* aa_node) {
    size_t node_offset = (size_t) & ((Node*)0)->node;
    return (Node*)((size_t)aa_node - node_offset);
  }

  // Returns the value for an AATreeNode.
  static size_t ValueFunction(AATreeNode* aa_node) {
    return static_cast<size_t>(NodeFromAATreeNode(aa_node)->value);
  }
};
