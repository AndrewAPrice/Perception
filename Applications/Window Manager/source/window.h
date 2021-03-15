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
#include <string>

#include "permebuf/Libraries/perception/devices/keyboard_listener.permebuf.h"
#include "permebuf/Libraries/perception/devices/mouse_listener.permebuf.h"
#include "permebuf/Libraries/perception/window.permebuf.h"
#include "types.h"

namespace perception {
class Font;
}

class Frame;

class Window {
public:
	static Window* CreateDialog(std::string_view title,
		int width, int height,
		uint32 background_color,
		::permebuf::perception::Window window_listener
		 = ::permebuf::perception::Window(),
		::permebuf::perception::devices::KeyboardListener keyboard_listener
		 = ::permebuf::perception::devices::KeyboardListener(),
		::permebuf::perception::devices::MouseListener mouse_listener
		 = ::permebuf::perception::devices::MouseListener());
	static Window* CreateWindow(std::string_view title,
		uint32 background_color,
		::permebuf::perception::Window window_listener
		 = ::permebuf::perception::Window(),
		::permebuf::perception::devices::KeyboardListener keyboard_listener
		 = ::permebuf::perception::devices::KeyboardListener(),
		::permebuf::perception::devices::MouseListener mouse_listener
		 = ::permebuf::perception::devices::MouseListener());

	static Window* GetWindow(
		const ::permebuf::perception::Window& window_listener);

	void Focus();
	bool IsFocused();
	void Resized();
	void Close();
	static void UnfocusAllWindows();

	static bool ForEachFrontToBackDialog(const std::function<bool(Window&)>& on_each_dialog);
	static void ForEachBackToFrontDialog(const std::function<void(Window&)>& on_each_dialog);
	static Window* GetWindowBeingDragged();
	static void MouseNotHoveringOverWindowContents();
	void DraggedTo(int screen_x, int screen_y);
	void DroppedAt(int screen_x, int screen_y);
	bool MouseEvent(int screen_x, int screen_y,
		::permebuf::perception::devices::MouseButton button,
		bool is_button_down);
	void HandleTabClick(int offset_along_tab,
		int original_tab_x, int original_tab_y);

	void Draw(int min_x, int min_y, int max_x, int max_y);
	void InvalidateDialogAndTitle();
	void InvalidateContents(int min_x, int min_y, int max_x, int max_y);

	int GetX() { return x_; }
	int GetY() { return y_; }
	int GetWidth() { return width_; }
	int GetHeight() { return height_; }
	bool IsDialog() { return is_dialog_; }

	void SetTextureId(int texture_id);

private:
	friend Frame;

	static void DrawHeaderBackground(int x, int y, int width, uint32 color);

	void DrawDecorations(int min_x, int min_y, int max_x, int max_y);
	void DrawWindowContents(int x, int y, int min_x, int min_y, int max_x, int max_y);

	// The window's title.
	std::string title_;

	// The width of the window's title, in pixels.
	int title_width_;

	// The window's position.
	int x_;
	int y_;

	// The window's size.
	int width_;
	int height_;

	// Is the window a dialog?
	bool is_dialog_;

	// The frame this window is in. Not used for dialogs.
	Frame* frame_;

	// The texture representing the contents of this window.
	// 0 if unknown.
	size_t texture_id_;

	// Next/previous windows in the Z-order of things.
	Window* next_;
	Window* previous_;

	uint32 fill_color_;

	::permebuf::perception::Window window_listener_;

	::permebuf::perception::devices::KeyboardListener keyboard_listener_;

	::permebuf::perception::devices::MouseListener mouse_listener_;
};

void InitializeWindows();

::perception::Font* GetWindowTitleFont();
