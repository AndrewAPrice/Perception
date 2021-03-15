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

#include "compositor_quad_tree.h"

#include <algorithm>

// Adds a rectangle, splitting any rectangle that is partially covered,
// and removing any rectangle that is fully covered.
void CompositorQuadTree::AddOccludingRectangle(Rectangle* rect) {
	if (rect->max_x - rect->min_x <= 0 ||
		rect->max_y - rect->min_y <= 0) {
		rectangle_pool_.Release(rect);
		return;
	}
	rect->node = nullptr;
	ForEachOverlappingItem(rect, [&](Rectangle* overlapping_rect) {
		// Add each part that peaks out behind our new rectangle.
		CreateSubRectanglesForEachBackgroundPartThatPokesOut(
			overlapping_rect, rect);

		// Remove the old rectangle.
		Remove(overlapping_rect);
	});

	Add(rect);
}

// Tells a region that it needs to draw into the window manager's
// texture.
void CompositorQuadTree::DrawAreaToWindowManagerTexture(
	int min_x, int min_y, int max_x, int max_y) {
	if (max_x - min_x <= 0 ||
		max_y - min_y <= 0) {
		return;
	}

	Rectangle* rect = rectangle_pool_.Allocate();
	rect->min_x = min_x;
	rect->max_x = max_x;
	rect->min_y = min_y;
	rect->max_y = max_y;
			rect->node = nullptr;

	ForEachOverlappingItem(rect, [&](Rectangle* overlapping_rect) {
		if (overlapping_rect->draw_into_wm_texture) {
			// Already copying into the window manager's texture.
			return;
		}

		// Add each part that peaks out behind our new rectangle.
		CreateSubRectanglesForEachBackgroundPartThatPokesOut(
			overlapping_rect, rect);

		// Add the part of the rectangle that is fully enclosed in our
		// region that we want to draw into the window manager's
		// texture.
		Rectangle* new_part = rectangle_pool_.Allocate();
		new_part->SubRectangleOf(*overlapping_rect,
			std::max(overlapping_rect->min_x, rect->min_x),
			std::max(overlapping_rect->min_y, rect->min_y),
			std::min(overlapping_rect->max_x, rect->max_x),
			std::min(overlapping_rect->max_y, rect->max_y));
		new_part->draw_into_wm_texture = true;
		new_part->node = nullptr;
		Add(new_part);

		// Remove the old rectangle.
		Remove(overlapping_rect);
	});

	rectangle_pool_.Release(rect);
}

// Allocates a Rectangle from the object pool, for passing into
// AddOccludingRectangle.
Rectangle* CompositorQuadTree::AllocateRectangle() {
	return rectangle_pool_.Allocate();
}

// Creates a sub-rectangle for each background part that is visible behind
// the foreground. Make sure that the Rectangles at least overlap before
// calling this.
void CompositorQuadTree::CreateSubRectanglesForEachBackgroundPartThatPokesOut(
	Rectangle* background, Rectangle* foreground) {
	// Divides the rectangle up into 4 parts that could peak out:
	// #####
	// %%i**
	// @@@@@
	if (background->min_y < foreground->min_y) {
		// Some of the top peaks out.
		Rectangle* new_part = rectangle_pool_.Allocate();
		new_part->node = nullptr;
		new_part->SubRectangleOf(*background,
			background->min_x,
			background->min_y,
			background->max_x,
			foreground->min_y);
		Add(new_part);
	}

	if (background->max_y > foreground->max_y) {
		// Some of the bottom peaks out.
		Rectangle* new_part = rectangle_pool_.Allocate();
		new_part->node = nullptr;
		new_part->SubRectangleOf(*background,
			background->min_x,
			foreground->max_y,
			background->max_x,
			background->max_y);
		Add(new_part);
	}

	if (background->min_x < foreground->min_x) {
		// Some of the left peaks out.
		Rectangle* new_part = rectangle_pool_.Allocate();
		new_part->node = nullptr;
		new_part->SubRectangleOf(*background,
			background->min_x,
			std::max(background->min_y, foreground->min_y),
			foreground->min_x,
			std::min(background->max_y, foreground->max_y));
		Add(new_part);
	}

	if (background->max_x > foreground->max_x) {
		// Some of the right peaks out.
		Rectangle* new_part = rectangle_pool_.Allocate();
		new_part->node = nullptr;
		new_part->SubRectangleOf(*background,
			foreground->max_x,
			std::max(background->min_y, foreground->min_y),
			background->max_x,
			std::min(background->max_y, foreground->max_y));
		Add(new_part);
	}
}
