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

#include "text_terminal.h"

// #define DEBUG

// Uncomment to enable a very simple emulation with a linked list. Used to
#define LINKED_LIST

#ifdef LINKED_LIST

void InitializeAATree(struct AATree* tree) { tree->root = NULL; }

void InsertNodeIntoAATree(struct AATree* tree, struct AATreeNode* node,
                          size_t (*value_function)(struct AATreeNode* node)) {
#ifdef DEBUG
  PrintString("Inserting ");
  // PrintHex((size_t)node);
  PrintChar('\n');
#endif
  if (node->parent != NULL) {
    if (node->parent == tree) {
      PrintString("Adding node back to same tree without removing.\n");
    } else {
      PrintString("Adding node to a different tree without removing.\n");
    }
  }
  node->parent = (struct AATreeNode*)tree;
  node->next = tree->root;
  node->previous = NULL;
  tree->root = node;
  if (node->next != NULL) node->next->previous = node;
#ifdef DEBUG
  PrintAATree(tree, value_function);
#endif
}

extern void RemoveNodeFromAATree(
    struct AATree* tree, struct AATreeNode* node,
    size_t (*value_function)(struct AATreeNode* node)) {
#ifdef DEBUG
  PrintString("Removing ");
  PrintHex((size_t)node);
  PrintChar('\n');
#endif
  if (node->parent != (struct ATTreeNode*)tree) {
    if (node->parent == NULL) {
      PrintString("Removing node that isn't in any tree.\n");
    } else {
      PrintString("Removing node that is in another tree.\n");
    }
  }
  node->parent = NULL;
  if (node->previous == NULL) {
    tree->root = node->next;
  } else {
    node->previous->next = node->next;
  }

  if (node->next != NULL) {
    node->next->previous = node->previous;
  }
#ifdef DEBUG
  PrintAATree(tree, value_function);
#endif
}

struct AATreeNode* SearchForNodeLessThanOrEqualToValue(
    struct AATree* tree, size_t value,
    size_t (*value_function)(struct AATreeNode* node)) {
  struct AATreeNode* node = tree->root;

  struct AATreeNode* closest = NULL;
  size_t closest_value = 0;

  while (node != NULL) {
    size_t current_value = value_function(node);
    if (current_value == value) {
      return node;
    }
    if (current_value < value &&
        (current_value > closest_value || closest == NULL)) {
      closest = node;
      closest_value = current_value;
    }
    node = node->next;
  }

  return closest;
}

struct AATreeNode* SearchForNodeGreaterThanOrEqualToValue(
    struct AATree* tree, size_t value,
    size_t (*value_function)(struct AATreeNode* node)) {
  struct AATreeNode* node = tree->root;

  struct AATreeNode* closest = NULL;
  size_t closest_value = 0;

  while (node != NULL) {
    size_t current_value = value_function(node);
    if (current_value == value) {
      return node;
    }
    if (current_value > value &&
        (current_value < closest_value || closest == NULL)) {
      closest = node;
      closest_value = current_value;
    }
    node = node->next;
  }

  return closest;
}

struct AATreeNode* SearchForNodeEqualToValue(
    struct AATree* tree, size_t value,
    size_t (*value_function)(struct AATreeNode* node)) {
  struct AATreeNode* node = tree->root;
  while (node != NULL) {
    size_t current_value = value_function(node);
    if (current_value == value) {
      return node;
    }
    node = node->next;
  }

  return NULL;
}

void PrintAATree(struct AATree* tree,
                 size_t (*value_function)(struct AATreeNode* node)) {
  PrintString("Linked list ");
  PrintHex((size_t)tree);
  PrintString(" has ");
  int count = 0;
  struct AATreeNode* node = tree->root;
  while (node != NULL) {
    PrintHex(node);
    PrintChar(' ');
    count++;
    node = node->next;
  }
  PrintNumber(count);
  PrintString(" node(s).\n");
}

#else

void PrintAATreeNode(struct AATreeNode* node,
                     size_t (*value_function)(struct AATreeNode* node),
                     int indentation) {
  return;
  if (node == NULL) return;
  for (int i = 0; i < indentation; i++) PrintChar(' ');

  PrintString("Value: ");
  PrintNumber(value_function(node));
  PrintString(" Count: ");
  int count = 1;
  struct AATreeNode* next_node = node->next;
  while (next_node != NULL) {
    count++;
    next_node = next_node->next;
  }
  PrintNumber(count);
  PrintString(" Level:");
  PrintNumber(node->level);
  PrintChar('\n');
  PrintAATreeNode(node->left, value_function, indentation + 1);
  PrintAATreeNode(node->right, value_function, indentation + 1);
}

void PrintAATree(struct AATree* tree,
                 size_t (*value_function)(struct AATreeNode* node)) {
  PrintString("Tree: ");
  PrintHex((size_t)tree);
  PrintString(":\n");
  PrintAATreeNode(tree->root, value_function, 1);
}

void InitializeAATree(struct AATree* tree) { tree->root = 0; }

struct AATreeNode* MaybeSkewAANode(struct AATreeNode* node) {
  if (node->left != NULL && node->left->level == node->level) {
    // Swap the pointers of the horizontal left links.
    struct AATreeNode* new_parent = node->left;
    node->left = new_parent->right;
    new_parent->right = node;

    // Update the parents.
    new_parent->parent = node->parent;
    if (node->left != NULL) node->left->parent = node;
    node->parent = new_parent;

    return new_parent;
  }

  return node;
}

struct AATreeNode* MaybeSplitAANode(struct AATreeNode* node) {
  if (node->right != NULL && node->right->right != NULL &&
      node->level == node->right->right->level) {
    // We have two horiziontal right links. Make the middle node the new parent.

    struct AATreeNode* new_parent = node->right;
    node->right = new_parent->left;

    new_parent->left = node;
    new_parent->level++;

    // Update the parents.
    new_parent->parent = node->parent;
    if (node->right != NULL) node->right->parent = node;
    node->parent = new_parent;

    return new_parent;
  }
  return node;
}

struct AATreeNode* InsertNodeIntoAANode(
    struct AATreeNode* parent, struct AATreeNode* node_to_insert,
    size_t value_being_inserted,
    size_t (*value_function)(struct AATreeNode* node)) {
  if (parent == NULL) return node_to_insert;

  size_t parent_value = value_function(parent);
  if (value_being_inserted == parent_value) {
    // Duplicate entry. Make this the new parent and make the new node the front
    // of a linked list of nodes with the same value.

    // Copy the existing node's tree fields over to the new node.
    node_to_insert->left = parent->left;
    node_to_insert->right = parent->right;
    node_to_insert->parent = parent->parent;
    if (node_to_insert->left != NULL)
      node_to_insert->left->parent = node_to_insert;
    if (node_to_insert->right != NULL)
      node_to_insert->right->parent = node_to_insert;

    // Make this node the first one in the linked list.
    parent->previous = node_to_insert;
    node_to_insert->next = parent;

    return node_to_insert;
  } else if (value_being_inserted < parent_value) {
    parent->left = InsertNodeIntoAANode(parent->left, node_to_insert,
                                        value_being_inserted, value_function);
    parent->left->parent = parent;
  } else {
    parent->right = InsertNodeIntoAANode(parent->right, node_to_insert,
                                         value_being_inserted, value_function);
    parent->right->parent = parent;
  }

  return parent;  // MaybeSplitAANode(MaybeSkewAANode(parent));
}

void InsertNodeIntoAATree(struct AATree* tree, struct AATreeNode* node,
                          size_t (*value_function)(struct AATreeNode* node)) {
  node->left = node->right = node->previous = node->next = 0;
  node->level = 1;
  if (tree->root == NULL) {
    // The tree is otherwise empty, so this will be the first and only
    // node.
    tree->root = node;
    node->parent = 0;
  } else {
    size_t value_being_inserted = value_function(node);

    tree->root = InsertNodeIntoAANode(tree->root, node, value_being_inserted,
                                      value_function);
  }
}

void MaybeDecreaseAANodeLevel(struct AATreeNode* node) {
  uint8 left_level = node->left->level;
  uint8 right_level = node->right->level;
  uint8 should_be = (left_level < right_level ? left_level : right_level) + 1;

  if (should_be < node->level) {
    node->level = should_be;
    if (should_be < node->right->level) node->right->level = should_be;
  }
}

struct AATreeNode* GetPredecessorOfAANode(struct AATreeNode* node) {
  node = node->left;
  while (node->right != NULL) node = node->right;
  return node;
}

struct AATreeNode* GetSuccessorOfAANode(struct AATreeNode* node) {
  node = node->right;
  while (node->left != NULL) node = node->left;
  return node;
}

struct AATreeNode* RemoveNodeWithValueFromBelowAANode(
    struct AATreeNode* node, size_t node_to_delete_value,
    size_t (*value_function)(struct AATreeNode* node)) {
  if (node == NULL) return 0;

  size_t node_value = value_function(node);
  if (node_to_delete_value == node_value) {
    if (node->left == NULL) {
      if (node->right == NULL) {
        // This is a leaf, so return null.
        return 0;
      } else {
        // Grab the next highest value node from the right.
        struct AATreeNode* new_node = GetSuccessorOfAANode(node);
        // Remove the new node from the right.
        struct AATreeNode* new_right = RemoveNodeWithValueFromBelowAANode(
            node->right, value_function(new_node), value_function);

        // Put the new node in the same position of the tree as this node.
        new_node->left = node->left;
        new_node->right = new_right;

        node = new_node;
      }
    } else {
      // Grab the next lowest value node from the left.
      struct AATreeNode* new_node = GetPredecessorOfAANode(node);
      // Remove the new node from the left.
      struct AATreeNode* new_left = RemoveNodeWithValueFromBelowAANode(
          node->left, value_function(new_node), value_function);

      // Put the new node in the same position of the tree as this node.
      new_node->left = new_left;
      new_node->right = node->right;

      node = new_node;
    }

    // Let the child nodes know who their new parent is.
    if (node->left != NULL) node->left->parent = node;
    if (node->right != NULL) node->right->parent = node;

  } else if (node_to_delete_value > node_value) {
    // Walk down the left side.
    node->right = RemoveNodeWithValueFromBelowAANode(
        node->right, node_to_delete_value, value_function);
    if (node->right != NULL) node->right->parent = node;
  } else {
    // Walk down the right side.
    node->left = RemoveNodeWithValueFromBelowAANode(
        node->left, node_to_delete_value, value_function);
    if (node->left != NULL) node->left->parent = node;
  }
  /*
    MaybeDecreaseAANodeLevel(node);
    node = MaybeSkewAANode(node);
    if (node->right != NULL) {
      node->right = MaybeSkewAANode(node->right);
      if (node->right->right != NULL) {
        node->right->right = MaybeSkewAANode(node->right->right);
      }
    }
    node = MaybeSplitAANode(node);
    if (node->right != NULL) {
      node->right = MaybeSplitAANode(node->right);
    }*/
  return node;
}

void RemoveNodeFromAATree(struct AATree* tree, struct AATreeNode* node,
                          size_t (*value_function)(struct AATreeNode* node)) {
  if (node->previous != NULL) {
    // Multiple nodes have the same value, and we're not at the front of the
    // linked list. So just remove us from the linked list and need to update
    // the tree structure.
    node->previous->next = node->next;
    if (node->next != NULL) node->next->previous = node->previous;
  } else if (node->next != NULL) {
    // Multiple nodes have the same value but we're at the front of the linked
    // list. Swap the next item in the linked list for us.
    struct AATreeNode* next = node->next;
    next->previous = 0;
    next->left = node->left;
    if (next->left != NULL) next->left->parent = next;
    next->right = node->right;
    if (next->right != NULL) next->right->parent = next;
    next->parent = node->parent;
    if (next->parent == NULL) {
      // We're the root node.
      tree->root = next;
    } else {
      struct AATreeNode* parent = next->parent;
      if (parent->left == node) {
        parent->left = next;
      } else {
        parent->right = next;
      }
    }
  } else {
    // We're the only node with this value, so remove us from the tree.
    tree->root = RemoveNodeWithValueFromBelowAANode(
        tree->root, value_function(node), value_function);
    if (tree->root != NULL) tree->root->parent = 0;
  }
}

struct AATreeNode* SearchForNodeLessThanOrEqualToValue(
    struct AATree* tree, size_t value,
    size_t (*value_function)(struct AATreeNode* node)) {
  // Try to find an exact match, and if one doesn't exist, return the the
  // heighest valued node we found along the way that was below the value.

  // The backup node if we don't find one.
  size_t highest_suitable_node_value = 0;
  struct AATreeNode* highest_suitable_node = tree->root;

  struct AATreeNode* current_node = tree->root;
  while (current_node != NULL) {
    size_t current_value = value_function(current_node);
    if (current_value == value) {
      return current_node;  // Exact match.
    } else if (value < current_value) {
      // We're looking for a lower valued node.
      if (current_value > highest_suitable_node_value ||
          highest_suitable_node == NULL) {
        // This is the largest node we've found so far that's less than the
        // target value.
        highest_suitable_node_value = current_value;
        highest_suitable_node = current_node;
      }
      current_node = current_node->left;
    } else {
      // We're looking for a higher valued node.
      current_node = current_node->right;
    }
  }

  // Couldn't find an exact match so return the next smallest.
  return highest_suitable_node;
}

struct AATreeNode* SearchForNodeGreaterThanOrEqualToValue(
    struct AATree* tree, size_t value,
    size_t (*value_function)(struct AATreeNode* node)) {
  // Try to find an exact match, and if one doesn't exist, return the the
  // smallest node we found along the way that was below the value.

  // The backup node if we don't find one.
  size_t lowest_suitable_node_value = 0;
  struct AATreeNode* lowest_suitable_node = tree->root;

  struct AATreeNode* current_node = tree->root;
  while (current_node != NULL) {
    size_t current_value = value_function(current_node);
    if (current_value == value) {
      return current_node;  // Exact match.
    } else if (value < current_value) {
      // We're looking for a lower valued node.
      current_node = current_node->left;
    } else {
      // We're looking for a higher valuesd node.
      if (current_value < lowest_suitable_node_value ||
          lowest_suitable_node == NULL) {
        // This is the largest node we've found so far that's less than the
        // target value.
        lowest_suitable_node_value = current_value;
        lowest_suitable_node = current_node;
      }
      current_node = current_node->right;
    }
  }

  // Couldn't find an exact match so return the next smallest.
  return lowest_suitable_node;
}

struct AATreeNode* SearchForNodeEqualToValue(
    struct AATree* tree, size_t value,
    size_t (*value_function)(struct AATreeNode* node)) {
  // Try to find an exact match.

  struct AATreeNode* current_node = tree->root;
  while (current_node != NULL) {
    size_t current_value = value_function(current_node);
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
  return 0;
}

#endif