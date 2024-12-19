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

#include "text_terminal.h"

struct LinkedListNode {
  LinkedListNode *previous, *next;
};

template <class C, LinkedListNode C::* node_member>
class LinkedList {
 public:
  LinkedList() : first_node_(nullptr), last_node_(nullptr) {}

  ~LinkedList() {
    if (!IsEmpty()) print << "LinkedList being deallocated while not empty.\n";
  }

  void AddFront(C* item) {
    auto* node = ItemToNode(item);
    if (IsEmpty()) return InsertAsOnlyNode(node);

    node->previous = nullptr;
    node->next = first_node_;

    first_node_->previous = node;
    first_node_ = node;
  }

  void AddBack(C* item) {
    auto* node = ItemToNode(item);
    if (IsEmpty()) return InsertAsOnlyNode(node);

    node->previous = last_node_;
    node->next = nullptr;

    last_node_->next = node;
    last_node_ = node;
  }

  void InsertBefore(C* next_item, C* item) {
    if (next_item == nullptr) return AddBack(item);

    auto* next_node = ItemToNode(next_item);
    if (next_node == first_node_) return AddFront(item);

    auto* item_node = ItemToNode(item);
    item_node->previous = next_node->previous;
    item_node->next = next_node->next;

    next_node->previous->next = item_node;
    next_node->previous = item_node;
  }

  void InsertAfter(C* previous_item, C* item) {
    if (previous_item == nullptr) return AddFront(item);

    auto* previous_node = ItemToNode(previous_item);
    if (previous_node == last_node_) return AddBack(item);

    auto* item_node = ItemToNode(item);
    item_node->previous = previous_node;
    item_node->next = previous_node->next;

    previous_node->next->previous = item_node;
    previous_node->next = item_node;
  }

  void Remove(C* item) {
    auto* node = ItemToNode(item);

    if (node->previous) {
      node->previous->next = node->next;
    } else {
      first_node_ = node->next;
    }

    if (node->next) {
      node->next->previous = node->previous;
    } else {
      last_node_ = node->previous;
    }

    node->previous = nullptr;
    node->next = nullptr;
  }

  C* PopFront() {
    if (IsEmpty()) return nullptr;
    C* front = FirstItem();
    Remove(front);
    return front;
  }

  C* PopBack() {
    if (IsEmpty()) return nullptr;
    C* back = LastItem();
    Remove(back);
    return back;
  }

  bool IsEmpty() const { return first_node_ == nullptr; }

  C* FirstItem() {
    if (IsEmpty()) return nullptr;
    return NodeToItem(first_node_);
  }

  C* LastItem() {
    if (IsEmpty()) return nullptr;
    return NodeToItem(last_node_);
  }

  C* NextItem(C* item) {
    auto* next_node = ItemToNode(item)->next;
    if (next_node == nullptr) return nullptr;
    return NodeToItem(next_node);
  }

  C* PreviousItem(C* item) {
    auto* previous_node = ItemToNode(item)->previous;
    if (previous_node == nullptr) return nullptr;
    return NodeToItem(previous_node);
  }

  class Iterator {
   public:
    Iterator(LinkedList* list, C* item) : list_(list), item_(item) {}

    C* operator*() const { return item_; }

    Iterator& operator++() {
      item_ = list_->NextItem(item_);
      return *this;
    }

    bool operator==(const Iterator& other) const {
      return item_ == other.item_;
    }

    bool operator!=(const Iterator& other) const { return !(*this == other); }

   private:
    LinkedList* list_;
    C* item_;
  };

  Iterator begin() { return Iterator(this, FirstItem()); }

  Iterator end() { return Iterator(this, nullptr); }

 private:
  void InsertAsOnlyNode(LinkedListNode* node) {
    first_node_ = last_node_ = node;
    node->previous = node->next = nullptr;
  }

  LinkedListNode* ItemToNode(C* item) {
    return static_cast<LinkedListNode*>(&(item->*node_member));
  }

  C* NodeToItem(LinkedListNode* node) {
    return (C*)((size_t)node - OffsetOfNodeInItem());
  }

  size_t OffsetOfNodeInItem() {
    return (size_t)&(static_cast<C*>(0)->*node_member);
  }

  LinkedListNode *first_node_, *last_node_;
};