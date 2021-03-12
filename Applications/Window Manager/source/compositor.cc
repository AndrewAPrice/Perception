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

#include "compositor.h"

#include "frame.h"
#include "highlighter.h"
#include "mouse.h"
#include "perception/draw.h"
#include "perception/object_pool.h"
#include "permebuf/Libraries/perception/devices/graphics_driver.permebuf.h"
#include "screen.h"
#include "types.h"
#include "window.h"

using ::perception::FillRectangle;
using ::perception::DrawSprite1bitAlpha;
using ::perception::ObjectPool;
using ::permebuf::perception::devices::GraphicsDriver;
using ::permebuf::perception::devices::GraphicsCommand;

namespace {

bool has_invalidated_area;
int invalidated_area_min_x;
int invalidated_area_min_y;
int invalidated_area_max_x;
int invalidated_area_max_y;

class QuadTreeNode;

struct DrawRectangle {
	// Linked list of rectangles, either in the pool or in the QuadTreeNode.
	DrawRectangle* previous, *next;

	// The QuadTree node we're in.
	QuadTreeNode* node;

	// Linked list of rectangles (kept seperate from the other linked list as
	// we add/remove linked lists from the tree) for temporary iteration.
	DrawRectangle* temp_next;

	// The rectangle to draw to.
	int min_x, min_y, max_x, max_y;

	// The texture ID to copy to the output. May be 0 if we are a solid fill
	// color.
	size_t texture_id;

	// Coordinates in the texture to start copying from.
	int texture_x, texture_y;

	// Fixed color to fill with, if texture_id = 0.
	uint32 color;

	// Should this rectangle be drawn into the window manager's texture first?
	bool draw_into_wm_texture;

	// Is this a rectangle for a solid color?
	bool IsSolidColor() {
		return texture_id == 0;
	}

	// Does this rectangle overlap with another rectangle/
	bool Overlaps(const DrawRectangle& other) {
		return !(max_x <= other.min_x ||
			max_y <= other.min_y ||
			min_x >= other.max_x ||
			min_y >= other.max_y);
	}

	void SubRectangleOf(const DrawRectangle& other, int min_x_, int min_y_,
		int max_x_, int max_y_) {
		min_x = min_x_;
		min_y = min_y_;
		max_x = max_x_;
		max_y = max_y_;

		draw_into_wm_texture = other.draw_into_wm_texture;
		texture_id = other.texture_id;
		if (texture_id == 0) {
			color = other.color;
		} else {
			texture_x = other.texture_x + min_x - other.min_x;
			texture_y = other.texture_y + min_y - other.min_y;
		}
	}
};


void DrawBackground(int min_x, int min_y, int max_x, int max_y) {
	DrawSolidColor(min_x, min_y, max_x, max_y, kBackgroundColor);
}

struct QuadTreeNode {
	int min_x, min_y, max_x, max_y;
	QuadTreeNode* children[4];
	QuadTreeNode* parent;
	DrawRectangle* items;

	bool Overlaps(const DrawRectangle& other) {
		return !(max_x <= other.min_x ||
			max_y <= other.min_y ||
			min_x >= other.max_x ||
			min_y >= other.max_y);
	}

	bool Contains(const DrawRectangle& other) {
		return min_x <= other.min_x &&
			min_y <= other.min_y &&
			max_x >= other.max_x &&
			max_y >= other.max_y;
	}
};

ObjectPool<DrawRectangle> draw_rectangles;
ObjectPool<QuadTreeNode> quad_tree_nodes;


// Linked list of free QuadTreeNodes
class QuadTree {
public:
	QuadTree() : root_(nullptr) {}
	~QuadTree() {
		Reset();
	}

	void Reset() {
		Release(root_);
		root_ = nullptr;
	}

	void AddRectangle(DrawRectangle* rect) {
		if (rect->max_x - rect->min_x <= 0 ||
			rect->max_y - rect->min_y <= 0) {
			draw_rectangles.Release(rect);
			return;
		}
				rect->node = nullptr;

		ForEachOverlappingRectangle(rect, [&](DrawRectangle* overlapping_rect) {
			// Divides the rectangle up into 4 parts that could peak out:
			// #####
			// %%i**
			// @@@@@
			if (overlapping_rect->min_y < rect->min_y) {
				// Some of the top peaks out.
				DrawRectangle* new_part = draw_rectangles.Allocate();
				new_part->node = nullptr;
				new_part->SubRectangleOf(*overlapping_rect,
					overlapping_rect->min_x,
					overlapping_rect->min_y,
					overlapping_rect->max_x,
					rect->min_y);
				AddRectangleWithoutClipping(new_part);
			}

			if (overlapping_rect->max_y > rect->max_y) {
				// Some of the bottom peaks out.
				DrawRectangle* new_part = draw_rectangles.Allocate();
				new_part->node = nullptr;
				new_part->SubRectangleOf(*overlapping_rect,
					overlapping_rect->min_x,
					rect->max_y,
					overlapping_rect->max_x,
					overlapping_rect->max_y);
				AddRectangleWithoutClipping(new_part);
			}

			if (overlapping_rect->min_x < rect->min_x) {
				// Some of the left peaks out.
				DrawRectangle* new_part = draw_rectangles.Allocate();
				new_part->node = nullptr;
				new_part->SubRectangleOf(*overlapping_rect,
					overlapping_rect->min_x,
					std::max(overlapping_rect->min_y, rect->min_y),
					rect->min_x,
					std::min(overlapping_rect->max_y, rect->max_y));
				AddRectangleWithoutClipping(new_part);
			}

			if (overlapping_rect->max_x > rect->max_x) {
				// Some of the right peaks out.
				DrawRectangle* new_part = draw_rectangles.Allocate();
				new_part->node = nullptr;
				new_part->SubRectangleOf(*overlapping_rect,
					rect->max_x,
					std::max(overlapping_rect->min_y, rect->min_y),
					overlapping_rect->max_x,
					std::min(overlapping_rect->max_y, rect->max_y));
				AddRectangleWithoutClipping(new_part);
			}

			// Remove the old rectangle.
			RemoveRectangle(overlapping_rect);
		});

		AddRectangleWithoutClipping(rect);
	}

	void DrawToWindowManagerTexture(int min_x, int min_y, int max_x,
		int max_y) {
		if (max_x - min_x <= 0 ||
			max_y - min_y <= 0) {
			return;
		}

		DrawRectangle* rect = draw_rectangles.Allocate();
		rect->min_x = min_x;
		rect->max_x = max_x;
		rect->min_y = min_y;
		rect->max_y = max_y;
				rect->node = nullptr;

		ForEachOverlappingRectangle(rect, [&](DrawRectangle* overlapping_rect) {
			if (overlapping_rect->draw_into_wm_texture) {
				// Already copying into the window manager's texture.
				return;
			}

			// Divides the rectangle up into 4 parts that could peak out:
			// #####
			// %%i**
			// @@@@@
			if (overlapping_rect->min_y < rect->min_y) {
				// Some of the top peaks out.
				DrawRectangle* new_part = draw_rectangles.Allocate();
				new_part->SubRectangleOf(*overlapping_rect,
					overlapping_rect->min_x,
					overlapping_rect->min_y,
					overlapping_rect->max_x,
					rect->min_y);
				new_part->node = nullptr;
				AddRectangleWithoutClipping(new_part);
			}

			if (overlapping_rect->max_y > rect->max_y) {
				// Some of the bottom peaks out.
				DrawRectangle* new_part = draw_rectangles.Allocate();
				new_part->SubRectangleOf(*overlapping_rect,
					overlapping_rect->min_x,
					rect->max_y,
					overlapping_rect->max_x,
					overlapping_rect->max_y);
				new_part->node = nullptr;
				AddRectangleWithoutClipping(new_part);
			}

			if (overlapping_rect->min_x < rect->min_x) {
				// Some of the left peaks out.
				DrawRectangle* new_part = draw_rectangles.Allocate();
				new_part->SubRectangleOf(*overlapping_rect,
					overlapping_rect->min_x,
					std::max(overlapping_rect->min_y, rect->min_y),
					rect->min_x,
					std::min(overlapping_rect->max_y, rect->max_y));
				new_part->node = nullptr;
				AddRectangleWithoutClipping(new_part);
			}

			if (overlapping_rect->max_x > rect->max_x) {
				// Some of the right peaks out.
				DrawRectangle* new_part = draw_rectangles.Allocate();
				new_part->SubRectangleOf(*overlapping_rect,
					rect->max_x,
					std::max(overlapping_rect->min_y, rect->min_y),
					overlapping_rect->max_x,
					std::min(overlapping_rect->max_y, rect->max_y));
				new_part->node = nullptr;
				AddRectangleWithoutClipping(new_part);
			}

			// Add the rectangle that we need to draw to the window manger's
			// texture.
			DrawRectangle* new_part = draw_rectangles.Allocate();
			new_part->SubRectangleOf(*overlapping_rect,
				std::max(overlapping_rect->min_x, rect->min_x),
				std::max(overlapping_rect->min_y, rect->min_y),
				std::min(overlapping_rect->max_x, rect->max_x),
				std::min(overlapping_rect->max_y, rect->max_y));
			new_part->draw_into_wm_texture = true;
			new_part->node = nullptr;
			AddRectangleWithoutClipping(new_part);

			RemoveRectangle(overlapping_rect);
		});

		draw_rectangles.Release(rect);
	}

	void ForEachRectangle(
		const std::function<void(DrawRectangle* old_rect)>& on_each_rect) {
		ForEachRectangleInNode(root_, on_each_rect);
	}

private:
	QuadTreeNode* root_;

	void Release(QuadTreeNode *node) {
		if (node == nullptr)
			return;

		DrawRectangle* item = node->items;
		while (item != nullptr) {
			DrawRectangle* next = item->next;
			draw_rectangles.Release(item);
			item = next;
		}

		for (int i = 0; i < 4; i++)
			Release(node->children[i]);

		quad_tree_nodes.Release(node);
	}

	void ForEachRectangleInNode(QuadTreeNode* node,
		const std::function<void(DrawRectangle* old_rect)>& on_each_rect) {
		if (node == nullptr)
			return;
		DrawRectangle* item = node->items;
		while (item != nullptr) {
			DrawRectangle* next = item->next;
			on_each_rect(item);
			item = next;
		}
		for (int i = 0; i < 4; i++)
			ForEachRectangleInNode(node->children[i], on_each_rect);
	}

	void AddRectangleWithoutClipping(DrawRectangle* rect) {
		int width = rect->max_x - rect->min_x;
		int height = rect->max_y - rect->min_y;
		if (width <= 0 || height <= 0) {
			draw_rectangles.Release(rect);
			return;
		}

		int size = std::max(width, height);

		if (root_ == nullptr) {
			// First item being added to the quadtree.
			root_ = quad_tree_nodes.Allocate();
			for (int i = 0; i < 4; i++)
				root_->children[i] = nullptr;
			root_->min_x = rect->min_x;
			root_->min_y = rect->min_y;
			root_->max_x = rect->min_x + size;
			root_->max_y = rect->min_y + size;
			root_->parent = nullptr;
			root_->items = rect;

			rect->previous = nullptr;
			rect->next = nullptr;
			rect->node = root_;

		} else {
			QuadTreeNode* node = root_;
			while (true) {
				int node_size = node->max_x - node->min_x;

				 /* std::cout << "Testing rectangle " << rect->min_x << "," <<
					rect->min_y << " -> " << rect->max_x << "," <<
					rect->max_y << " of size " << size << " in node " <<
					node->min_x << "," << node->min_y << " -> " <<
					node->max_x << "," << node->max_y << " of size " << 
					(node_size * 3 / 4) << " -> " <<
					node_size << std::endl; */

				if (!node->Contains(*rect)) {
					// Too big for this node.
					if (node->parent == nullptr) {
						// We need to create a parent.
						bool to_the_left = rect->min_x < node->min_x;
						bool to_the_top = rect->min_y < node->min_y;
						int new_size = node_size * 4 / 3;

						QuadTreeNode* parent = quad_tree_nodes.Allocate();
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
						rect->previous = nullptr;
						rect->next = node->items;
						rect->node = node;
						if (node->items != nullptr)
							node->items->previous = rect;
						node->items = rect;
						return;
					} else {
						// Too small for this node, find the perfect child.
						bool to_the_right =
							rect->min_x > node->max_x - child_node_size;
						bool to_the_bottom =
							rect->min_y > node->max_y - child_node_size;

						if (to_the_right) {
							if (to_the_bottom) {
								// We belong in the bottom right child.
								if (node->children[0] == nullptr) {
									QuadTreeNode* child =
										quad_tree_nodes.Allocate();
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
									QuadTreeNode* child =
										quad_tree_nodes.Allocate();
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
									QuadTreeNode* child =
										quad_tree_nodes.Allocate();
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
									QuadTreeNode* child =
										quad_tree_nodes.Allocate();
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

	void RemoveRectangle(DrawRectangle* rect) {
		QuadTreeNode* node = rect->node;

		if (rect->next) {
			rect->next->previous = rect->previous;
		}
		if (rect->previous) {
			rect->previous->next = rect->next;
		} else {
			node->items = rect->next;

			if (node->items == nullptr) {
				// There are no more items in this QuadTreeNode, we might be
				// able to remove it.
				MaybeRemoveNode(node);
			}
		}

		draw_rectangles.Release(rect);
	}

	void MaybeRemoveNode(QuadTreeNode* node) {
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
		quad_tree_nodes.Release(node);
	}

	void ForEachOverlappingRectangle(DrawRectangle* new_rect,
		const std::function<void(DrawRectangle* old_rect)>& on_each_rect) {
		DrawRectangle* overlapping_rect = nullptr;
		ForEachOverlappingRectangleInNode(new_rect, root_,
			overlapping_rect);

		while (overlapping_rect != nullptr) {
			DrawRectangle* next = overlapping_rect->temp_next;
			on_each_rect(overlapping_rect);
			overlapping_rect = next;
		}
	}

	void ForEachOverlappingRectangleInNode(DrawRectangle* new_rect,
		QuadTreeNode* node,
		DrawRectangle*& last_overlapping_rect) {
		if (node == nullptr)
			return;

		if (!node->Overlaps(*new_rect))
			return;

		for (DrawRectangle* item = node->items;
			item != nullptr;
			item = item->next) {
			if (item->Overlaps(*new_rect)) {
				item->temp_next = last_overlapping_rect;
				last_overlapping_rect = item;
			}
		}

		for (int i = 0; i < 4; i++) {
			ForEachOverlappingRectangleInNode(new_rect, node->children[i],
				last_overlapping_rect);
		}
	}
};

QuadTree quad_tree;

}

void InitializeCompositor() {
	has_invalidated_area = false;
	quad_tree = QuadTree();
}

void InvalidateScreen(int min_x, int min_y, int max_x, int max_y) {
	if (has_invalidated_area) {
		invalidated_area_min_x = std::min(min_x, invalidated_area_min_x);
		invalidated_area_min_y = std::min(min_y, invalidated_area_min_y);
		invalidated_area_max_x = std::max(max_x, invalidated_area_max_x);
		invalidated_area_max_y = std::max(max_y, invalidated_area_max_y);
	} else {
		invalidated_area_min_x = min_x;
		invalidated_area_min_y = min_y;
		invalidated_area_max_x = max_x;
		invalidated_area_max_y = max_y;
		has_invalidated_area = true;
	}
}

void DrawScreen() {
	if (!has_invalidated_area)
		return;

	SleepUntilWeAreReadyToStartDrawing();

	has_invalidated_area = false;
	int min_x = std::max(0, invalidated_area_min_x);
	int min_y = std::max(0, invalidated_area_min_y);
	int max_x = std::min(GetScreenWidth(), invalidated_area_max_x);
	int max_y = std::min(GetScreenHeight(), invalidated_area_max_y);

	DrawBackground(min_x, min_y, max_x, max_y);

	Frame* root_frame = Frame::GetRootFrame();
	if (root_frame)
		root_frame->Draw(min_x, min_y, max_x, max_y);

	Window::ForEachBackToFrontDialog([&](Window& window) {
		window.Draw(min_x, min_y, max_x, max_y);
	});

	// Prep the overlays for drawing, which will mark which areas need to be
	// drawn to the window manager's texture and not directly to the screen.
	PrepHighlighterForDrawing(min_x, min_y, max_x, max_y);
	PrepMouseForDrawing(min_x, min_y, max_x, max_y);

	// Draw the screen.
	Permebuf<GraphicsDriver::RunCommandsMessage> commands;

	// There are 3 stages of commands that we want to construct:
	// (1) Draw any rectangles into the WM Texture.
	// (2) Draw mouse/highlighter into the WM Texture.
	// (3) Draw WM textures into framebuffer.
	// (4) Draw window textures into framebuffer.

	PermebufListOfOneOfs<::permebuf::perception::devices::GraphicsCommand>
		first_draw_into_wm_texture_command, last_draw_into_wm_texture_command;
	bool has_commands_to_draw_into_wm_texture = false;
	PermebufListOfOneOfs<::permebuf::perception::devices::GraphicsCommand>
		first_draw_wm_texture_into_framebuffer_command,
		last_draw_wm_texture_into_framebuffer_command;
	bool has_commands_to_draw_wm_into_frame_buffer = false;
	PermebufListOfOneOfs<::permebuf::perception::devices::GraphicsCommand>
		first_draw_into_framebuffer_command,
		last_draw_into_framebuffer_command;
	bool has_commands_to_draw_into_framebuffer = false;	

	size_t texture_drawing_into_window_manager = 0;
	size_t texture_drawing_into_framebuffer = 0;

	quad_tree.ForEachRectangle([&](DrawRectangle* rectangle) {
		if (rectangle->draw_into_wm_texture) {
			// Draw this into the WM texture.
			if (rectangle->texture_id != GetWindowManagerTextureId()) {
				// This is NOT the window manager texture, which is already
				// applied to the window manager texture.

				// Draw this rectangle into the window manager's texture.
				if (has_commands_to_draw_into_wm_texture) {
					last_draw_into_wm_texture_command =
						last_draw_into_wm_texture_command.InsertAfter();
				} else {
					first_draw_into_wm_texture_command = 
						last_draw_into_wm_texture_command =
						commands.AllocateListOfOneOfs<
							::permebuf::perception::devices::GraphicsCommand>();

					has_commands_to_draw_into_wm_texture = true;
				}

				if (rectangle->IsSolidColor()) {
					// Draw solid color into window manager texture.
					auto command_one_of =
						commands.AllocateOneOf<GraphicsCommand>();
					last_draw_into_wm_texture_command.Set(
						command_one_of);
					auto command = command_one_of.MutableFillRectangle();
					command.SetLeft(rectangle->min_x);
					command.SetTop(rectangle->min_y);
					command.SetRight(rectangle->max_x);
					command.SetBottom(rectangle->max_y);
					command.SetColor(rectangle->color);
				} else {
					// Copy texture into window manager's texture.
					if (rectangle->texture_id !=
						texture_drawing_into_window_manager) {
						// Switch over to source texture.
						texture_drawing_into_window_manager =
							rectangle->texture_id;

						auto command_one_of =
							commands.AllocateOneOf<GraphicsCommand>();
						last_draw_into_wm_texture_command.Set(
							command_one_of);
						command_one_of.MutableSetSourceTexture()
							.SetTexture(rectangle->texture_id);

						last_draw_into_wm_texture_command =
							last_draw_into_wm_texture_command.
								InsertAfter();
					}

					auto command_one_of =
						commands.AllocateOneOf<GraphicsCommand>();
					last_draw_into_wm_texture_command.Set(
						command_one_of);
					auto command = command_one_of.MutableCopyPartOfATexture();
					command.SetLeftDestination(rectangle->min_x);
					command.SetTopDestination(rectangle->min_y);
					command.SetLeftSource(rectangle->texture_x);
					command.SetTopSource(rectangle->texture_y);
					command.SetWidth(rectangle->max_x - rectangle->min_x);
					command.SetHeight(rectangle->max_y - rectangle->min_y);
				}
			}

			// Now copy this area from the Window Manager's texture into the
			// framebuffer.
			if (has_commands_to_draw_wm_into_frame_buffer) {
				last_draw_wm_texture_into_framebuffer_command =
					last_draw_wm_texture_into_framebuffer_command.
						InsertAfter();
			} else {
				first_draw_wm_texture_into_framebuffer_command =
					last_draw_wm_texture_into_framebuffer_command =
					commands.AllocateListOfOneOfs<
						::permebuf::perception::devices::GraphicsCommand>();
				has_commands_to_draw_wm_into_frame_buffer = true;
			}

			auto command_one_of =
				commands.AllocateOneOf<GraphicsCommand>();
			last_draw_wm_texture_into_framebuffer_command.Set(
				command_one_of);
			auto command = command_one_of.MutableCopyPartOfATexture();
			command.SetLeftSource(rectangle->min_x);
			command.SetTopSource(rectangle->min_y);
			command.SetLeftDestination(rectangle->min_x);
			command.SetTopDestination(rectangle->min_y);
			command.SetWidth(rectangle->max_x - rectangle->min_x);
			command.SetHeight(rectangle->max_y - rectangle->min_y);
		} else {
			// Draw this rectangle straight into the framebuffer.
			if (has_commands_to_draw_into_framebuffer) {
				last_draw_into_framebuffer_command =
					last_draw_into_framebuffer_command.
						InsertAfter();
			} else {
				first_draw_into_framebuffer_command =
					last_draw_into_framebuffer_command =
					commands.AllocateListOfOneOfs<
						::permebuf::perception::devices::GraphicsCommand>();
				has_commands_to_draw_into_framebuffer = true;
			}

			if (rectangle->IsSolidColor()) {
				// Draw solid color into framebuffer.
				auto command_one_of = commands.AllocateOneOf<GraphicsCommand>();
				last_draw_into_framebuffer_command.Set(
					command_one_of);
				auto command = command_one_of.MutableFillRectangle();
				command.SetLeft(rectangle->min_x);
				command.SetTop(rectangle->min_y);
				command.SetRight(rectangle->max_x);
				command.SetBottom(rectangle->max_y);
				command.SetColor(rectangle->color);
			} else {
				// Copy texture into framebuffer.
				if (rectangle->texture_id != texture_drawing_into_framebuffer) {
					// Switch over the source texture.
					texture_drawing_into_framebuffer = rectangle->texture_id;

					auto command_one_of =
						commands.AllocateOneOf<GraphicsCommand>();
					last_draw_into_framebuffer_command.Set(
						command_one_of);
					command_one_of.MutableSetSourceTexture()
						.SetTexture(rectangle->texture_id);

					last_draw_into_framebuffer_command =
						last_draw_into_framebuffer_command.
							InsertAfter();
				}

				auto command_one_of = commands.AllocateOneOf<GraphicsCommand>();
				last_draw_into_framebuffer_command.Set(
					command_one_of);
				auto command = command_one_of.MutableCopyPartOfATexture();
				command.SetLeftSource(rectangle->min_x);
				command.SetTopSource(rectangle->min_y);
				command.SetLeftDestination(rectangle->texture_x);
				command.SetTopDestination(rectangle->texture_y);
				command.SetWidth(rectangle->max_x - rectangle->min_x);
				command.SetHeight(rectangle->max_y - rectangle->min_y);
			}
		}
	 });

	// Merge all the draw commands together.
	PermebufListOfOneOfs<::permebuf::perception::devices::GraphicsCommand>
		last_draw_command;

	if (has_commands_to_draw_into_wm_texture) {
		// We have things to draw into the window manager's texture.

		// Set destination to be the wm texture.
		last_draw_command = commands->MutableCommands();
		auto command_one_of = commands.AllocateOneOf<GraphicsCommand>();
		last_draw_command.Set(command_one_of);
		command_one_of.MutableSetDestinationTexture()
			.SetTexture(GetWindowManagerTextureId());

		// Chain the commands onto the end.
		last_draw_command.SetNext(first_draw_into_wm_texture_command);
		last_draw_command = last_draw_into_wm_texture_command;
	}

	// Draw some overlays.
	DrawHighlighter(commands, last_draw_command, min_x, min_y, max_x,
		max_y);
	DrawMouse(commands, last_draw_command, min_x, min_y, max_x, max_y);

	// Set the destination to be the framebuffer.
	if (last_draw_command.IsValid()) {
		last_draw_command = last_draw_command.InsertAfter();
	} else {
		last_draw_command = commands->MutableCommands();
	}
	auto set_destination_to_framebuffer_command_one_of =
		commands.AllocateOneOf<GraphicsCommand>();
	last_draw_command.Set(set_destination_to_framebuffer_command_one_of);
	set_destination_to_framebuffer_command_one_of
		.MutableSetDestinationTexture().SetTexture(0);  // The screen.

	if (has_commands_to_draw_wm_into_frame_buffer) {
		// Set the source to be the window manager's texture.
		last_draw_command = last_draw_command.InsertAfter();
		auto command_one_of = commands.AllocateOneOf<GraphicsCommand>();
		last_draw_command.Set(command_one_of);
		command_one_of.MutableSetSourceTexture().SetTexture(
			GetWindowManagerTextureId());

		// Chain the commands onto the end.
		last_draw_command.SetNext(
			first_draw_wm_texture_into_framebuffer_command);
		last_draw_command =
			last_draw_wm_texture_into_framebuffer_command;
	}

	if (has_commands_to_draw_into_framebuffer) {
		// Chain the commands onto the end.
		last_draw_command.SetNext(first_draw_into_framebuffer_command);
		last_draw_command = last_draw_into_framebuffer_command;
	}

	RunDrawCommands(std::move(commands));

	// Reset the quad tree.
	quad_tree.Reset();
}


// Draws a solid color on the screen.
void DrawSolidColor(int min_x, int min_y, int max_x, int max_y,
	uint32 fill_color) {
	DrawRectangle* rectangle = draw_rectangles.Allocate();
	rectangle->min_x = min_x;
	rectangle->min_y = min_y;
	rectangle->max_x = max_x;
	rectangle->max_y = max_y;
	rectangle->texture_id = 0;
	rectangle->color = fill_color;
	rectangle->draw_into_wm_texture = false;
	quad_tree.AddRectangle(rectangle);
}

// Copies a part of a texture onto the screen.
void CopyTexture(int min_x, int min_y, int max_x, int max_y, size_t texture_id,
	int texture_x, int texture_y) {
	DrawRectangle* rectangle = draw_rectangles.Allocate();
	rectangle->min_x = min_x;
	rectangle->min_y = min_y;
	rectangle->max_x = max_x;
	rectangle->max_y = max_y;
	rectangle->texture_id = texture_id;
	rectangle->texture_x = texture_x;
	rectangle->texture_y = texture_y;

	if (texture_id == GetWindowManagerTextureId()) {
		// We're copying from the window manager's texture, so we can consider
		// the same as textures that we are going to draw into the window
		// manager.
		rectangle->draw_into_wm_texture = true;
	} else {
		rectangle->draw_into_wm_texture = false;
	}
	quad_tree.AddRectangle(rectangle);
}


void CopySectionOfScreenIntoWindowManagersTexture(int min_x, int min_y,
	int max_x, int max_y) {
	quad_tree.DrawToWindowManagerTexture(min_x, min_y, max_x, max_y);	
}