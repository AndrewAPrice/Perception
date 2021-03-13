// Copyright 2021 Google LLC
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

#include <algorithm>

namespace perception {

// Represents a quadtree.
template <class T>
class QuadTree {
public:
	struct Node;

	struct Object {
		// Linked list of items.
		T* previous, *next;

		// Linked list of item (kept seperate from the other linked list as
		// we add/remove items from the tree) for temporary iteration.
		T *next_temp;

		// The bounding box of this object.
		int min_x, min_y, max_x, max_y;

		// The node that we're in.
		Node* node;

		// Does this object overlap with another object?
		bool Overlaps(const T& other) {
			return !(max_x <= other.min_x ||
				max_y <= other.min_y ||
				min_x >= other.max_x ||
				min_y >= other.max_y);
		}
	};

	struct Node {
		Node* parent;
		Node* children[4];
		T* items;
		int min_x, min_y, max_x, max_y;

		bool Overlaps(const T& other) {
			return !(max_x <= other.min_x ||
				max_y <= other.min_y ||
				min_x >= other.max_x ||
				min_y >= other.max_y);
		}

		bool Contains(const T& other) {
			return min_x <= other.min_x &&
				min_y <= other.min_y &&
				max_x >= other.max_x &&
				max_y >= other.max_y;
		}
	};


	QuadTree(ObjectPool<T>* object_pool) :
		root_(nullptr), object_pool_ (object_pool_) {}

	virtual ~QuadTree() {
		Reset();
	}

	void Reset() {
		Release(root_);
		root_ = nullptr;
	}

	void Add(T* item) {
		int width = item->max_x - item->min_x;
		int height = item->max_y - item->min_y;
		if (width <= 0 || height <= 0) {
			object_pool_->Release(item);
			return;
		}

		int size = std::max(width, height);

		if (root_ == nullptr) {
			// First item being added to the quadtree.
			root_ = node_pool_.Allocate();
			for (int i = 0; i < 4; i++)
				root_->children[i] = nullptr;
			root_->min_x = item->min_x;
			root_->min_y = item->min_y;
			root_->max_x = item->min_x + size;
			root_->max_y = item->min_y + size;
			root_->parent = nullptr;
			root_->items = item;

			item->previous = nullptr;
			item->next = nullptr;
			item->node = root_;

		} else {
			Node* node = root_;
			while (true) {
				int node_size = node->max_x - node->min_x;

				 /* std::cout << "Testing item " << item->min_x << "," <<
					item->min_y << " -> " << item->max_x << "," <<
					item->max_y << " of size " << size << " in node " <<
					node->min_x << "," << node->min_y << " -> " <<
					node->max_x << "," << node->max_y << " of size " << 
					(node_size * 3 / 4) << " -> " <<
					node_size << std::endl; */

				if (!node->Contains(*item)) {
					// Too big for this node.
					if (node->parent == nullptr) {
						// We need to create a parent.
						bool to_the_left = item->min_x < node->min_x;
						bool to_the_top = item->min_y < node->min_y;
						int new_size = node_size * 4 / 3;

						Node* parent = node_pool_.Allocate();
						root_ = parent;
						node->parent = parent;
						parent->parent = nullptr;
						parent->items = nullptr;
						for (int i = 0; i < 4; i++)
							parent->children[i] = nullptr;

						if (to_the_left) {
							if (to_the_top) {
								// Expand up and left.
								parent->min_x = node->max_x - new_size;
								parent->min_y = node->max_y - new_size;
								parent->max_x = node->max_x;
								parent->max_y = node->max_y;

								// Bottom right child is the current node.
								parent->children[0] = node;
							} else {
								// Expand down and left.
								parent->min_x = node->max_x - new_size;
								parent->min_y = node->min_y;
								parent->max_x = node->max_x;
								parent->max_x = node->min_y + new_size;

								// Top right child is the current node.
								parent->children[1] = node;
							}
						} else {
							if (to_the_top) {
								// Expand up and right.
								parent->min_x = node->min_x;
								parent->min_y = node->max_y - new_size;
								parent->max_x = node->min_x + new_size;
								parent->max_y = node->max_y;

								// Bottom left child is the current node.
								parent->children[2] = node;
							} else {
								// Expand down and right.
								parent->min_x = node->min_x;
								parent->min_y = node->min_y;
								parent->max_x = node->min_x + new_size;
								parent->max_y = node->min_y + new_size;

								// Top left child is the current node.
								parent->children[3] = node;
							}
						}
						node = parent;
					} else {
						node = node->parent;
					}
				} else {
					int child_node_size = node_size * 3 / 4;
					if (size >= node_size / 2) {
						// Perfect size for this node, we'll add ourselves here.
						item->previous = nullptr;
						item->next = node->items;
						item->node = node;
						if (node->items != nullptr)
							node->items->previous = item;
						node->items = item;
						return;
					} else {
						// Too small for this node, find the perfect child.
						bool to_the_right =
							item->min_x > node->max_x - child_node_size;
						bool to_the_bottom =
							item->min_y > node->max_y - child_node_size;

						if (to_the_right) {
							if (to_the_bottom) {
								// We belong in the bottom right child.
								if (node->children[0] == nullptr) {
									Node* child =
										node_pool_.Allocate();
									child->parent = node;
									for (int i = 0; i < 4; i++)
										child->children[i] = nullptr;
									child->items = nullptr;

									child->min_x = node->max_x -
										child_node_size;
									child->min_y = node->max_y -
										child_node_size;
									child->max_x = node->max_x;
									child->max_y = node->max_y;

									node->children[0] = child;
									node = child;
								} else {
									node = node->children[0];
								}
							} else {
								// We belong in the top right child.
								if (node->children[1] == nullptr) {
									Node* child =
										node_pool_.Allocate();
									child->parent = node;
									for (int i = 0; i < 4; i++)
										child->children[i] = nullptr;
									child->items = nullptr;

									child->min_x = node->max_x -
										child_node_size;
									child->min_y = node->min_y;
									child->max_x = node->max_x;
									child->max_y = node->min_y +
										child_node_size;

									node->children[1] = child;
									node = child;
								} else {
									node = node->children[1];
								}
							}
						} else {
							if (to_the_bottom) {
								// We belong in the bottom left child.
								if (node->children[2] == nullptr) {
									Node* child =
										node_pool_.Allocate();
									child->parent = node;
									for (int i = 0; i < 4; i++)
										child->children[i] = nullptr;
									child->items = nullptr;

									child->min_x = node->min_x;
									child->min_y = node->max_y -
										child_node_size;
									child->max_x = node->min_x +
										child_node_size;
									child->max_y = node->max_y;

									node->children[2] = child;
									node = child;
								} else {
									node = node->children[2];
								}
							} else {
								// We belong in the top left child.
								if (node->children[3] == nullptr) {
									Node* child =
										node_pool_.Allocate();
									child->parent = node;
									for (int i = 0; i < 4; i++)
										child->children[i] = nullptr;
									child->items = nullptr;

									child->min_x = node->min_x;
									child->min_y = node->min_y;
									child->max_x = node->min_x +
										child_node_size;
									child->max_y = node->min_y +
										child_node_size;

									node->children[3] = child;
									node = child;
								} else {
									node = node->children[3];
								}
							}

						}
					}
				}

			}
		}
	}

	void Remove(T* item) {
		Node* node = item->node;

		if (item->next) {
			item->next->previous = item->previous;
		}
		if (item->previous) {
			item->previous->next = item->next;
		} else {
			node->items = item->next;

			if (node->items == nullptr) {
				// There are no more items in this Node, we might be
				// able to remove it.
				MaybeRemoveNode(node);
			}
		}

		object_pool_->Release(item);
	}

	void ForEachItem(
		const std::function<void(T*)>& on_each_item) {
		ForEachItemInNode(root_, on_each_item);
	}

protected:
	Node* root_;
	ObjectPool<T>* object_pool_;
	ObjectPool<Node> node_pool_;

	void Release(Node *node) {
		if (node == nullptr)
			return;

		T* item = node->items;
		while (item != nullptr) {
			T* next = item->next;
			object_pool_->Release(item);
			item = next;
		}

		for (int i = 0; i < 4; i++)
			Release(node->children[i]);

		node_pool_.Release(node);
	}

	void ForEachItemInNode(Node* node,
		const std::function<void(T*)>& on_each_item) {
		if (node == nullptr)
			return;
		T* item = node->items;
		while (item != nullptr) {
			T* next = item->next;
			on_each_item(item);
			item = next;
		}
		for (int i = 0; i < 4; i++)
			ForEachItemInNode(node->children[i], on_each_item);
	}

	void MaybeRemoveNode(Node* node) {
		if (node == nullptr)
			return;

		if (node->items != nullptr) {
			// There are still items in this node.
			return;
		}

		for (int i = 0; i < 4; i++) {
			if (node->children[i] != nullptr) {
				// We still have children.
				return;
			}
		}

		// There are no items and no children, so this is an empty node that
		// we can delete.
		if (node->parent == nullptr) {
			// Parent node.
			root_ = nullptr;
		} else {
			// Remove us from our parent.
			for (int i = 0; i < 4; i++) {
				if (node->parent->children[i] == node) {
					node->parent->children[i] = nullptr;
				}
			}

			// Maybe we can clean up our parent.
			MaybeRemoveNode(node->parent);
		}
		node_pool_.Release(node);
	}

	void ForEachOverlappingItem(T* new_item,
		const std::function<void(T*)>& on_each_item) {
		T* overlapping_item = nullptr;
		ForEachOverlappingItemInNode(new_item, root_,
			overlapping_item);

		while (overlapping_item != nullptr) {
			T* next = overlapping_item->next_temp;
			on_each_item(overlapping_item);
			overlapping_item = next;
		}
	}

	void ForEachOverlappingItemInNode(T* new_item,
		Node* node, T*& last_overlapping_item) {
		if (node == nullptr)
			return;

		if (!node->Overlaps(*new_item))
			return;

		for (T* item = node->items;
			item != nullptr;
			item = item->next) {
			if (item->Overlaps(*new_item)) {
				item->next_temp = last_overlapping_item;
				last_overlapping_item = item;
			}
		}

		for (int i = 0; i < 4; i++) {
			ForEachOverlappingItemInNode(new_item,
				node->children[i],
				last_overlapping_item);
		}
	}
};

}