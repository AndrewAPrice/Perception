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

#include "text_terminal.h"
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

// An AA tree, which is a self balancing binary tree.
template <class C, AATreeNode C::* node_member, size_t C::* value_member>
struct AATree {
 public:
  AATree() : root_(nullptr) {}

  size_t CountNodes() { return CountNodes(root_); }

  void Insert(C* item) {
#ifdef DEBUG
    print << "Inserting node\n";
#endif
    auto node = ItemToNode(item);
    node->left = node->right = node->previous = node->next = nullptr;
    node->level = 1;
    if (root_ == nullptr) {
      // The tree is otherwise empty, so this will be the first and only
      // node.
      root_ = node;
      node->parent = nullptr;
    } else {
      size_t value_being_inserted = ValueOfNode(node);
      root_ = InsertNodeIntoAANode(root_, node, value_being_inserted);
      root_->parent = nullptr;
    }

#ifdef DEBUG
    PrintAATree(tree);
#endif
  }

  void Remove(C* item) {
#ifdef DEBUG
    print << "Removing node\n";
#endif
    auto node = ItemToNode(item);
    if (node->previous != nullptr) {
      // Multiple nodes have the same value, and we're not at the front of the
      // linked list. So just remove us from the linked list and need to update
      // the tree structure.
      node->previous->next = node->next;
      if (node->next != nullptr) node->next->previous = node->previous;
    } else if (node->next != nullptr) {
      // Multiple nodes have the same value but we're at the front of the linked
      // list. Swap the next item in the linked list for us.
      AATreeNode* next = node->next;
      next->previous = nullptr;
      next->left = node->left;
      if (next->left != nullptr) next->left->parent = next;
      next->right = node->right;
      if (next->right != nullptr) next->right->parent = next;
      next->parent = node->parent;
      if (next->parent == nullptr) {
        // We're the root node.
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
      // We're the only node with this value, so remove us from the tree.
      root_ = RemoveNodeWithValueFromBelowAANode(root_, ValueOfNode(node));
      if (root_ != nullptr) root_->parent = nullptr;
    }

#ifdef DEBUG
    PrintAATree(tree, value_function);
#endif
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
    print << "Tree: " << NumberFormat::Hexidecimal << (size_t)this << '\n';
    PrintAATreeNode(root_, '*', 1);
  }

 private:
  AATreeNode* SearchForNodeLessThanOrEqualToValue(size_t value) {
    // Try to find an exact match, and if one doesn't exist, return the the
    // heighest valued node we found along the way that was below the value.

    // The backup node if we don't find one.
    size_t highest_suitable_node_value = 0;
    AATreeNode* highest_suitable_node = nullptr;

    AATreeNode* current_node = root_;
    while (current_node != nullptr) {
      size_t current_value = ValueOfNode(current_node);
      if (current_value == value) return current_node;  // Exact match.

      // Not a match but test if it's the closest we've found that is less than.
      if (current_value < value &&
          (current_value > highest_suitable_node_value ||
           highest_suitable_node == nullptr)) {
        // This is the largest node we've found so far that's less than the
        // target value.
        highest_suitable_node_value = current_value;
        highest_suitable_node = current_node;
      }

      if (value < current_value) {
        current_node = current_node->left;
      } else {
        // We're looking for a higher valued node.
        current_node = current_node->right;
      }
    }

    // Couldn't find an exact match so return the next smallest.
    return highest_suitable_node;
  }

  AATreeNode* SearchForNodeGreaterThanOrEqualToValue(size_t value) {
    // Try to find an exact match, and if one doesn't exist, return the the
    // smallest node we found along the way that was below the value.

    // The backup node if we don't find one.
    size_t lowest_suitable_node_value = 0;
    AATreeNode* lowest_suitable_node = nullptr;
    AATreeNode* current_node = root_;

    while (current_node != nullptr) {
      size_t current_value = ValueOfNode(current_node);
      if (current_value == value) {
        return current_node;  // Exact match.
      }

      // Not a match but test if it's the closest we've found that is greater
      // than.
      if (current_value > value &&
          (current_value < lowest_suitable_node_value ||
           lowest_suitable_node == nullptr)) {
        // This is the largest node we've found so far that's less than the
        // target value.
        lowest_suitable_node_value = current_value;
        lowest_suitable_node = current_node;
      }

      if (value < current_value) {
        // We're looking for a lower valued node.
        current_node = current_node->left;
      } else {
        current_node = current_node->right;
      }
    }

    // Couldn't find an exact match so return the next smallest.
    return lowest_suitable_node;
  }

  AATreeNode* SearchForNodeEqualToValue(size_t value) {
    // Try to find an exact match.

    AATreeNode* current_node = root_;
    while (current_node != nullptr) {
      size_t current_value = ValueOfNode(current_node);
      if (current_value == value) {
        return current_node;  // Exact match.
      } else if (value < current_value) {
        // We're looking for a lower valued node.
        current_node = current_node->left;
      } else {
        current_node = current_node->right;
      }
    }

    // No node was found.
    return nullptr;
  }

  static size_t CountNodes(AATreeNode* node) {
    if (node == nullptr) return 0;

    size_t count = 1;
    AATreeNode* next_node = node->next;
    while (next_node != nullptr) {
      count++;
      next_node = next_node->next;
    }

    return CountNodes(node->left) + CountNodes(node->right) + count;
  }

  static AATreeNode* MaybeSkewAANode(AATreeNode* node) {
    if (node->left != nullptr && node->left->level == node->level) {
      // Swap the pointers of the horizontal left links.
      AATreeNode* new_parent = node->left;
      node->left = new_parent->right;
      new_parent->right = node;

      // Update the parents.
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
      // We have two horiziontal right links. Make the middle node the new
      // parent.

      AATreeNode* new_parent = node->right;
      node->right = new_parent->left;

      new_parent->left = node;
      new_parent->level++;

      // Update the parents.
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
    if (parent == nullptr) {
      // Stand-alone leaf node.
      return node_to_insert;
    }

    size_t parent_value = ValueOfNode(parent);
    if (value_being_inserted == parent_value) {
      // Duplicate entry. Make this the new parent and make the new node the
      // front of a linked list of nodes with the same value.

      // Copy the existing node's tree fields over to the new node.
      node_to_insert->left = parent->left;
      node_to_insert->right = parent->right;
      if (node_to_insert->left != nullptr)
        node_to_insert->left->parent = node_to_insert;
      if (node_to_insert->right != nullptr)
        node_to_insert->right->parent = node_to_insert;

      // Make this node the first one in the linked list.
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

  static AATreeNode* GetPredecessorOfAANode(AATreeNode* node) {
    node = node->left;
    while (node->right != nullptr) node = node->right;
    return node;
  }

  static AATreeNode* GetSuccessorOfAANode(AATreeNode* node) {
    node = node->right;
    while (node->left != nullptr) node = node->left;
    return node;
  }

  static AATreeNode* RemoveNodeWithValueFromBelowAANode(
      AATreeNode* node, size_t node_to_delete_value) {
    if (node == nullptr) return nullptr;

    size_t node_value = ValueOfNode(node);
    if (node_to_delete_value == node_value) {
      if (node->left == nullptr) {
        if (node->right == nullptr) {
          // This is a leaf, so return null.
          return nullptr;
        } else {
          // Grab the next lowest value node from the right.
          AATreeNode* new_node = GetSuccessorOfAANode(node);
          // Remove the new node from the right.
          AATreeNode* new_right = RemoveNodeWithValueFromBelowAANode(
              node->right, ValueOfNode(new_node));

          // Put the new node in the same position of the tree as this node.
          new_node->left = node->left;
          new_node->right = new_right;

          node = new_node;
        }
      } else {
        // Grab the next highest value node from the left.
        AATreeNode* new_node = GetPredecessorOfAANode(node);

        // Remove the new node from the left.
        AATreeNode* new_left = RemoveNodeWithValueFromBelowAANode(
            node->left, ValueOfNode(new_node));

        // Put the new node in the same position of the tree as this node.
        new_node->left = new_left;
        new_node->right = node->right;

        node = new_node;
      }

      // Let the child nodes know who their new parent is.
      if (node->left != nullptr) node->left->parent = node;
      if (node->right != nullptr) node->right->parent = node;

    } else if (node_to_delete_value > node_value) {
      // Walk down the left side.
      node->right =
          RemoveNodeWithValueFromBelowAANode(node->right, node_to_delete_value);
      if (node->right != nullptr) node->right->parent = node;
    } else {
      // Walk down the right side.
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

  void PrintAATreeNode(AATreeNode* node, char side, int indentation) {
    if (node == nullptr) return;
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
  }

  AATreeNode* root_;
};
