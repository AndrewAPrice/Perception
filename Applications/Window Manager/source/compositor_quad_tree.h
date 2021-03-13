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

#include <functional>

#include "perception/object_pool.h"
#include "perception/quadtree.h"
#include "types.h"

struct Rectangle : public ::perception::QuadTree<Rectangle>::Object {
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

	// Makes this rectangle a sub-rectangle of the other.
	void SubRectangleOf(const Rectangle& other, int min_x_, int min_y_,
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


class CompositorQuadTree : public ::perception::QuadTree<Rectangle> {
public:
	CompositorQuadTree() : QuadTree(&rectangle_pool_) {}

	virtual ~CompositorQuadTree() {}

	// Adds a rectangle, splitting any rectangle that is partially covered,
	// and removing any rectangle that is fully covered.
	void AddOccludingRectangle(Rectangle* rect);

	// Tells a region that it needs to draw into the window manager's
	// texture.
	void DrawAreaToWindowManagerTexture(int min_x, int min_y, int max_x,
		int max_y);

	// Allocates a Rectangle from the object pool, for passing into
	// AddOccludingRectangle.
	Rectangle* AllocateRectangle();

private:
	// Creates a sub-rectangle for each background part that is visible behind
	// the foreground. Make sure that the Rectangles at least overlap before
	// calling this.
	void CreateSubRectanglesForEachBackgroundPartThatPokesOut(
		Rectangle* background, Rectangle* foreground);

	::perception::ObjectPool<Rectangle> rectangle_pool_;
};
