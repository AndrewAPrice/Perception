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

#include "permebuf/Libraries/perception/devices/mouse_listener.permebuf.h"
#include "types.h"

class Window;

class Frame {
public:
	static Frame* GetRootFrame();
	
	static Frame* GetFrameBeingDragged();
	void DraggedTo(int screen_x, int screen_y);
	void DroppedAt(int screen_x, int screen_y);

	// Gets the area and frame we can drop this window into. If the area doesn't
	// match the frame, then turn the frame into a split frame.
	static Frame* GetDropFrame(
		const Window& window, int mouse_x, int mouse_y,
		int& min_x, int& min_y, int& max_x, int& max_y);
	static void DropInWindow(
		Window& window, int mouse_x, int mouse_y);

	void UpdateFrame(bool resized);

	void AddWindow(Window& window);
	void RemoveWindow(Window& window);

	static void AddWindowToLastFocusedFrame(Window& window);

	void MouseEvent(int screen_x, int screen_y,
		::permebuf::perception::devices::MouseButton button,
		bool is_button_down);

	void Draw(int min_x, int min_y, int max_x, int max_y);
	void Invalidate();

private:
	friend Window;

	bool IsValidSize(int width, int height);

	int LongestWindowTitleWidth();

	// Position of the frame.
	int x_;
	int y_;

	// Size of the frame.
	int width_;
	int height_;

	// The parent frame.
	Frame* parent_;

	// Is this a split frame or a dock frame?
	bool is_split_frame_;

	union {
		struct {
			Frame* child_a_;
			Frame* child_b_;
			// Direction we're split.
			bool is_split_vertically_;
			// Split percentage.
			float split_percent_;
			// Position of the split in pixels.
			int split_point_;

		} SplitFrame;
		struct {
			// The title height with all of the windows in them.
			int title_height_;

			// Linked list of all windows in this frame.
			Window* first_window_;
			Window* last_window_;

			// The currently focused window.
			Window* focused_window_;


		} DockFrame;
	};
};

void InitializeFrames();
