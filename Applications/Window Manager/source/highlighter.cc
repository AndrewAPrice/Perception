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

#include "highlighter.h"

#include "compositor.h"
#include "perception/draw.h"
#include "screen.h"

using ::perception::FillRectangleAlpha;

namespace {

bool highlighter_enabled;
int highlighter_min_x;
int highlighter_min_y;
int highlighter_max_x;
int highlighter_max_y;

}

void InitializeHighlighter() {
	highlighter_enabled = false;
}

void SetHighlighter(int min_x, int min_y, int max_x, int max_y) {
	if (highlighter_enabled) {
		if (highlighter_min_x == min_x &&
			highlighter_max_x == max_x &&
			highlighter_min_y == min_y &&
			highlighter_max_y == max_y) {
			return;
		}

		InvalidateScreen(highlighter_min_x, highlighter_min_y,
			highlighter_max_x, highlighter_max_y);
	}

	highlighter_min_x = min_x;
	highlighter_min_y = min_y;
	highlighter_max_x = max_x;
	highlighter_max_y = max_y;
	highlighter_enabled = true;

	InvalidateScreen(highlighter_min_x, highlighter_min_y,
		highlighter_max_x, highlighter_max_y);
}

void DisableHighlighter() {
	if (!highlighter_enabled)
		return;

	highlighter_enabled = false;
	InvalidateScreen(highlighter_min_x, highlighter_min_y,
		highlighter_max_x, highlighter_max_y);
}

void DrawHighlighter(int min_x, int min_y, int max_x, int max_y) {
	if (!highlighter_enabled)
		return;

	int draw_min_x = std::max(highlighter_min_x, min_x);
	int draw_min_y = std::max(highlighter_min_y, min_y);
	int draw_max_x = std::min(highlighter_max_x, max_x);
	int draw_max_y = std::min(highlighter_max_y, max_y);

	FillRectangleAlpha(draw_min_x, draw_min_y,
		draw_max_x, draw_max_y,
		HIGHLIGHTER_TINT,
		GetWindowManagerTextureData(),
		GetScreenWidth(), GetScreenHeight());
	
}