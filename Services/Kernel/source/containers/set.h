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

#include "containers/aa_tree.h"
#include "containers/linked_list.h"
#include "containers/object_pool.h"

namespace containers {

// A node in the set.
struct SetNode {
  // Value of this node.
  size_t value;

  // This node in the AATree.
  AATreeNode aa_tree_node;

  // This node in the linked list of all nodes.
  LinkedListNode linked_list_node;
};

// A set of values.
template <class T>
class Set {
 public:
  Set() {}

  ~Set() {
    while (!nodes_.IsEmpty()) ObjectPool<SetNode>::Release(nodes_.PopFront());
    size_ = 0;
  }

  // Inserts a value into the set. Does nothing if the value is already
  // inserted. Returns whether the value is in the set, which is false only if
  // there was no memory to grow the set.
  bool Insert(T value) {
    if (tree_.SearchForItemEqualToValue(static_cast<size_t>(value)) != nullptr)
      return true;  // Already inserted.

    SetNode* node = ObjectPool<SetNode>::Allocate();
    if (node == nullptr) return false;

    node->value = static_cast<size_t>(value);
    nodes_.AddBack(node);
    tree_.Insert(node);
    size_++;
    return true;
  }

  // Removes a value from the set. Does nothing if the value is not in the set.
  void Remove(T value) {
    SetNode* node = tree_.SearchForItemEqualToValue(static_cast<size_t>(value));
    if (node == nullptr) return;

    nodes_.Remove(node);
    tree_.Remove(node);
    ObjectPool<SetNode>::Release(node);
    size_--;
  }

  // Returns if a value is in a set.
  bool Contains(T value) {
    return tree_.SearchForItemEqualToValue(static_cast<size_t>(value)) !=
           nullptr;
  }

  // Returns how many values are in the set.
  size_t Size() const { return size_; }

 private:
  // Tree of nodes keyed by their value.
  AATree<SetNode, &SetNode::aa_tree_node, &SetNode::value> tree_;

  // All nodes.
  LinkedList<SetNode, &SetNode::linked_list_node> nodes_;

  // How many values are in the set.
  size_t size_ = 0;
};

}  // namespace containers

