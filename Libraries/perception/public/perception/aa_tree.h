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

#ifdef KERNEL
#include "text_terminal.h"
#else
#include <iostream>
#endif

#ifndef AA_TREE_LOOP_GUARD
#define AA_TREE_LOOP_GUARD(counter) \
  if (++counter > 100000) { \
    while(1); \
  }
#endif

namespace perception {

// A node in an AA-tree.
struct AATreeNode {
  // This node's level. This has to do with the tree structure and is not
  // related to the node's value.
  uint8 level;

  // The parent in the tree structure. If there are multiple nodes with the same
  // value, this field is only valid for the first node.
  AATreeNode* parent;

  // The child that is has a lower and higher value than this node. If there
  // are multiple nodes with the same value, this field is only valid for the
  // first node.
  AATreeNode *left, *right;

  // Linked list of nodes of the same value.
  AATreeNode *previous, *next;
};

// An AA tree, which is a self balancing binary tree.
template <class C, AATreeNode C::* node_member, size_t C::* value_member>
struct AATree {
 public:
  AATree() : root_(nullptr) {}

  size_t CountNodes() { return CountNodes(root_); }

  void Insert(C* item) {
    auto node = ItemToNode(item);
    node->left = node->right = node->previous = node->next = nullptr;
    node->level = 1;
    if (root_ == nullptr) {
      // The tree is otherwise empty, so this will be the first and only node.
      root_ = node;
      node->parent = nullptr;
    } else {
      size_t value_being_inserted = ValueOfNode(node);
      root_ = InsertNodeIntoAANode(root_, node, value_being_inserted);
      root_->parent = nullptr;
    }
  }

  void Remove(C* item) {
    auto node = ItemToNode(item);
    if (node->previous != nullptr) {
      // Multiple nodes have the same value, and this is not at the front of the
      // linked list. So just remove it from the linked list and the tree structure doesn't need to be updated.
      node->previous->next = node->next;
      if (node->next != nullptr) node->next->previous = node->previous;
    } else if (node->next != nullptr) {
      // Multiple nodes have the same value but this is at the front of the linked
      // list. Swap the next item in the linked list for this one.
      AATreeNode* next = node->next;
      next->previous = nullptr;
      next->left = node->left;
      next->level = node->level;
      if (next->left != nullptr) next->left->parent = next;
      next->right = node->right;
      if (next->right != nullptr) next->right->parent = next;
      next->parent = node->parent;
      if (next->parent == nullptr) {
        // This is the root node.
        root_ = next;
      } else {
        AATreeNode* parent = next->parent;
        if (parent->left == node) {
          parent->left = next;
        } else {
          parent->right = next;
        }
      }
    } else {
      // This is the only node with this value, so remove it from the tree.
      root_ = RemoveNodeWithValueFromBelowAANode(root_, ValueOfNode(node));
      if (root_ != nullptr) root_->parent = nullptr;
    }
  }

  C* SearchForItemLessThanOrEqualToValue(size_t value) {
    AATreeNode* node = SearchForNodeLessThanOrEqualToValue(value);
    if (node == nullptr) return nullptr;
    return NodeToItem(node);
  }

  C* SearchForItemGreaterThanOrEqualToValue(size_t value) {
    AATreeNode* node = SearchForNodeGreaterThanOrEqualToValue(value);
    if (node == nullptr) return nullptr;
    return NodeToItem(node);
  }

  C* SearchForItemEqualToValue(size_t value) {
    AATreeNode* node = SearchForNodeEqualToValue(value);
    if (node == nullptr) return nullptr;
    return NodeToItem(node);
  }

  void PrintAATree() {
#ifdef KERNEL
    print << "Tree: " << NumberFormat::Hexidecimal << (size_t)this << '\n';
    PrintAATreeNode(root_, '*', 1);
#else
    std::cout << "Tree: 0x" << std::hex << (size_t)this << std::dec << "\n";
    PrintAATreeNode(root_, '*', 1);
#endif
  }

  // Returns whether the tree is empty.
  bool IsEmpty() const { return root_ == nullptr; }

  // Returns the first item in value order (item with the smallest value).
  // Returns nullptr if the tree is empty.
  C* FirstItem() {
    AATreeNode* current = root_;
    if (current == nullptr) return nullptr;
    int loop_counter = 0;
    while (current->left != nullptr) {
      AA_TREE_LOOP_GUARD(loop_counter);
      current = current->left;
    }
    return NodeToItem(current);
  }

  // Returns the last item in value order (item with the largest value).
  // Returns nullptr if the tree is empty.
  C* LastItem() {
    AATreeNode* current = root_;
    if (current == nullptr) return nullptr;
    int loop_counter1 = 0;
    while (current->right != nullptr) {
      AA_TREE_LOOP_GUARD(loop_counter1);
      current = current->right;
    }
    int loop_counter2 = 0;
    while (current->next != nullptr) {
      AA_TREE_LOOP_GUARD(loop_counter2);
      current = current->next;
    }
    return NodeToItem(current);
  }

  // Gets the next item in value order (in-order successor).
  // Returns nullptr if `item` is nullptr or there is no successor.
  C* NextItem(C* item) {
    if (item == nullptr) return nullptr;

    AATreeNode* node = ItemToNode(item);

    // If there is another node with the same value, return it.
    if (node->next != nullptr) return NodeToItem(node->next);

    // Find the head of the duplicate list for the current node.
    // In the tree structure, left/right/parent pointers are only valid on the head.
    AATreeNode* main_node = node;
    int loop_counter1 = 0;
    while (main_node->previous != nullptr) {
      AA_TREE_LOOP_GUARD(loop_counter1);
      main_node = main_node->previous;
    }

    // If the node has a right child, the successor is the leftmost node in the
    // right subtree.
    if (main_node->right != nullptr) {
      AATreeNode* current = main_node->right;
      int loop_counter2 = 0;
      while (current->left != nullptr) {
        AA_TREE_LOOP_GUARD(loop_counter2);
        current = current->left;
      }
      return NodeToItem(current);
    }

    // Otherwise, travel up parent pointers. Stop when we are the left child of a parent.
    AATreeNode* current = main_node;
    AATreeNode* parent = current->parent;
    int loop_counter3 = 0;
    while (parent != nullptr && current == parent->right) {
      AA_TREE_LOOP_GUARD(loop_counter3);
      current = parent;
      parent = parent->parent;
    }
    if (parent == nullptr) return nullptr;
    return NodeToItem(parent);
  }

  // Gets the previous item in value order (in-order predecessor).
  // Returns nullptr if `item` is nullptr or there is no predecessor.
  C* PreviousItem(C* item) {
    if (item == nullptr) return nullptr;

    AATreeNode* node = ItemToNode(item);

    // If there is a previous node with the same value, return it.
    if (node->previous != nullptr) return NodeToItem(node->previous);

    // If the node has a left child, the predecessor is the rightmost node in the left subtree.
    if (node->left != nullptr) {
      AATreeNode* current = node->left;
      int loop_counter1 = 0;
      while (current->right != nullptr) {
        AA_TREE_LOOP_GUARD(loop_counter1);
        current = current->right;
      }
      // Return the last element in its duplicate list.
      int loop_counter2 = 0;
      while (current->next != nullptr) {
        AA_TREE_LOOP_GUARD(loop_counter2);
        current = current->next;
      }
      return NodeToItem(current);
    }

    // Otherwise, travel up parent pointers. Stop when we are the right child of a parent.
    AATreeNode* current = node;
    AATreeNode* parent = current->parent;
    int loop_counter3 = 0;
    while (parent != nullptr && current == parent->left) {
      AA_TREE_LOOP_GUARD(loop_counter3);
      current = parent;
      parent = parent->parent;
    }
    if (parent == nullptr) return nullptr;
    // Predecessor is the parent (or the last element in its duplicate list).
    AATreeNode* last_dup = parent;
    int loop_counter4 = 0;
    while (last_dup->next != nullptr) {
      AA_TREE_LOOP_GUARD(loop_counter4);
      last_dup = last_dup->next;
    }
    return NodeToItem(last_dup);
  }

  // Iterator for range-based for loops.
  class Iterator {
   public:
    Iterator(AATree* tree, C* item) : tree_(tree), item_(item) {}

    C* operator*() const { return item_; }

    Iterator& operator++() {
      item_ = tree_->NextItem(item_);
      return *this;
    }

    bool operator!=(const Iterator& other) const {
      return item_ != other.item_;
    }

   private:
    AATree* tree_;
    C* item_;
  };

  Iterator begin() { return Iterator(this, FirstItem()); }
  Iterator end() { return Iterator(this, nullptr); }

 private:
  void PrintAATreeNode(AATreeNode* node, char side, int indentation) {
    if (node == nullptr) return;
#ifdef KERNEL
    for (int i = 0; i < indentation; i++) print << ' ';
    print << side;

    size_t value = ValueOfNode(node);
    print << " Value: " << NumberFormat::Decimal << value << "/"
          << NumberFormat::Hexidecimal << value << " Count: ";
    int count = 1;
    AATreeNode* next_node = node->next;
    while (next_node != nullptr) {
      count++;
      next_node = next_node->next;
    }
    print << NumberFormat::Decimal << count << " Level: " << node->level
          << '\n';
    PrintAATreeNode(node->left, 'l', indentation + 1);
    PrintAATreeNode(node->right, 'r', indentation + 1);
#else
    for (int i = 0; i < indentation; i++) std::cout << ' ';
    std::cout << side;

    size_t value = ValueOfNode(node);
    std::cout << " Value: " << std::dec << value << "/0x"
              << std::hex << value << std::dec << " Count: ";
    int count = 1;
    AATreeNode* next_node = node->next;
    while (next_node != nullptr) {
      count++;
      next_node = next_node->next;
    }
    std::cout << count << " Level: " << (int)node->level << '\n';
    PrintAATreeNode(node->left, 'l', indentation + 1);
    PrintAATreeNode(node->right, 'r', indentation + 1);
#endif
  }

  AATreeNode* SearchForNodeLessThanOrEqualToValue(size_t value) {
    size_t highest_suitable_node_value = 0;
    AATreeNode* highest_suitable_node = nullptr;

    AATreeNode* current_node = root_;
    int loop_counter = 0;
    while (current_node != nullptr) {
      AA_TREE_LOOP_GUARD(loop_counter);
      size_t current_value = ValueOfNode(current_node);
      if (current_value == value) return current_node;

      if (current_value < value &&
          (current_value > highest_suitable_node_value ||
           highest_suitable_node == nullptr)) {
        highest_suitable_node_value = current_value;
        highest_suitable_node = current_node;
      }

      if (value < current_value) {
        current_node = current_node->left;
      } else {
        current_node = current_node->right;
      }
    }
    return highest_suitable_node;
  }

  AATreeNode* SearchForNodeGreaterThanOrEqualToValue(size_t value) {
    size_t lowest_suitable_node_value = 0;
    AATreeNode* lowest_suitable_node = nullptr;
    AATreeNode* current_node = root_;
    int loop_counter = 0;

    while (current_node != nullptr) {
      AA_TREE_LOOP_GUARD(loop_counter);
      size_t current_value = ValueOfNode(current_node);
      if (current_value == value) return current_node;

      if (current_value > value &&
          (current_value < lowest_suitable_node_value ||
           lowest_suitable_node == nullptr)) {
        lowest_suitable_node_value = current_value;
        lowest_suitable_node = current_node;
      }

      if (value < current_value) {
        current_node = current_node->left;
      } else {
        current_node = current_node->right;
      }
    }
    return lowest_suitable_node;
  }

  AATreeNode* SearchForNodeEqualToValue(size_t value) {
    AATreeNode* current_node = root_;
    int loop_counter = 0;
    while (current_node != nullptr) {
      AA_TREE_LOOP_GUARD(loop_counter);
      size_t current_value = ValueOfNode(current_node);
      if (current_value == value) {
        return current_node;
      } else if (value < current_value) {
        current_node = current_node->left;
      } else {
        current_node = current_node->right;
      }
    }
    return nullptr;
  }

  static size_t CountNodes(AATreeNode* node) {
    if (node == nullptr) return 0;

    size_t count = 1;
    AATreeNode* next_node = node->next;
    int loop_counter = 0;
    while (next_node != nullptr) {
      AA_TREE_LOOP_GUARD(loop_counter);
      count++;
      next_node = next_node->next;
    }

    return CountNodes(node->left) + CountNodes(node->right) + count;
  }

  static AATreeNode* MaybeSkewAANode(AATreeNode* node) {
    if (node->left != nullptr && node->left->level == node->level) {
      AATreeNode* new_parent = node->left;
      node->left = new_parent->right;
      new_parent->right = node;

      new_parent->parent = node->parent;
      if (node->left != nullptr) node->left->parent = node;
      node->parent = new_parent;

      return new_parent;
    }
    return node;
  }

  static AATreeNode* MaybeSplitAANode(AATreeNode* node) {
    if (node->right != nullptr && node->right->right != nullptr &&
        node->level == node->right->right->level) {
      AATreeNode* new_parent = node->right;
      node->right = new_parent->left;

      new_parent->left = node;
      new_parent->level++;

      new_parent->parent = node->parent;
      if (node->right != nullptr) node->right->parent = node;
      node->parent = new_parent;

      return new_parent;
    }
    return node;
  }

  static AATreeNode* InsertNodeIntoAANode(AATreeNode* parent,
                                          AATreeNode* node_to_insert,
                                          size_t value_being_inserted) {
    if (parent == nullptr) return node_to_insert;

    size_t parent_value = ValueOfNode(parent);
    if (value_being_inserted == parent_value) {
      node_to_insert->left = parent->left;
      node_to_insert->right = parent->right;
      node_to_insert->level = parent->level;
      if (node_to_insert->left != nullptr)
        node_to_insert->left->parent = node_to_insert;
      if (node_to_insert->right != nullptr)
        node_to_insert->right->parent = node_to_insert;

      node_to_insert->previous = nullptr;
      parent->previous = node_to_insert;
      node_to_insert->next = parent;

      return node_to_insert;
    } else if (value_being_inserted < parent_value) {
      parent->left = InsertNodeIntoAANode(parent->left, node_to_insert,
                                          value_being_inserted);
      parent->left->parent = parent;
    } else {
      parent->right = InsertNodeIntoAANode(parent->right, node_to_insert,
                                           value_being_inserted);
      parent->right->parent = parent;
    }

    return MaybeSplitAANode(MaybeSkewAANode(parent));
  }

  static void MaybeDecreaseAANodeLevel(AATreeNode* node) {
    uint8 left_level = node->left != nullptr ? node->left->level : 0;
    uint8 right_level = node->right != nullptr ? node->right->level : 0;
    uint8 should_be = (left_level < right_level ? left_level : right_level) + 1;

    if (should_be < node->level) {
      node->level = should_be;
      if (node->right != nullptr && should_be < node->right->level)
        node->right->level = should_be;
    }
  }

  static AATreeNode* PredecessorOfAANode(AATreeNode* node) {
    node = node->left;
    int loop_counter = 0;
    while (node->right != nullptr) {
      AA_TREE_LOOP_GUARD(loop_counter);
      node = node->right;
    }
    return node;
  }

  static AATreeNode* SuccessorOfAANode(AATreeNode* node) {
    node = node->right;
    int loop_counter = 0;
    while (node->left != nullptr) {
      AA_TREE_LOOP_GUARD(loop_counter);
      node = node->left;
    }
    return node;
  }

  static AATreeNode* RemoveNodeWithValueFromBelowAANode(
      AATreeNode* node, size_t node_to_delete_value) {
    if (node == nullptr) return nullptr;

    size_t node_value = ValueOfNode(node);
    if (node_to_delete_value == node_value) {
      if (node->left == nullptr) {
        if (node->right == nullptr) {
          return nullptr;
        } else {
          AATreeNode* new_node = SuccessorOfAANode(node);
          AATreeNode* new_right = RemoveNodeWithValueFromBelowAANode(
              node->right, ValueOfNode(new_node));

          new_node->left = node->left;
          new_node->right = new_right;
          node = new_node;
        }
      } else {
        AATreeNode* new_node = PredecessorOfAANode(node);
        AATreeNode* new_left = RemoveNodeWithValueFromBelowAANode(
            node->left, ValueOfNode(new_node));

        new_node->left = new_left;
        new_node->right = node->right;
        node = new_node;
      }

      if (node->left != nullptr) node->left->parent = node;
      if (node->right != nullptr) node->right->parent = node;

    } else if (node_to_delete_value > node_value) {
      node->right =
          RemoveNodeWithValueFromBelowAANode(node->right, node_to_delete_value);
      if (node->right != nullptr) node->right->parent = node;
    } else {
      node->left =
          RemoveNodeWithValueFromBelowAANode(node->left, node_to_delete_value);
      if (node->left != nullptr) node->left->parent = node;
    }

    MaybeDecreaseAANodeLevel(node);
    node = MaybeSkewAANode(node);
    if (node->right != nullptr) {
      node->right = MaybeSkewAANode(node->right);
      if (node->right->right != nullptr) {
        node->right->right = MaybeSkewAANode(node->right->right);
      }
    }
    node = MaybeSplitAANode(node);
    if (node->right != nullptr) {
      node->right = MaybeSplitAANode(node->right);
    }
    return node;
  }

  static AATreeNode* ItemToNode(C* item) {
    return static_cast<AATreeNode*>(&(item->*node_member));
  }

  static C* NodeToItem(AATreeNode* node) {
    return (C*)((size_t)node - OffsetOfNodeInItem());
  }

  static size_t OffsetOfNodeInItem() {
    return (size_t)&(static_cast<C*>(0)->*node_member);
  }

  static size_t ValueOfNode(AATreeNode* node) {
    return NodeToItem(node)->*value_member;
  }

  AATreeNode* root_;
};

}  // namespace perception
