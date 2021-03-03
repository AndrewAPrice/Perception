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

#include "window.h"

#include "perception/draw.h"
#include "compositor.h"
#include "frame.h"
#include "highlighter.h"
#include "screen.h"

using ::perception::DrawXLine;
using ::perception::DrawXLineAlpha;
using ::perception::DrawYLine;
using ::perception::DrawYLineAlpha;
using ::perception::FillRectangle;
using ::permebuf::perception::devices::MouseButton;

namespace {

Window* dragging_window;
Window* focused_window;
Window* first_dialog;
Window* last_dialog;

// When dragging a dialog: offset
// When dragging a window: top left of the original title
int dragging_offset_x;
int dragging_offset_y;

}


Window* Window::CreateDialog(std::string_view title, int width, int height) {
	Window* window = new Window();
	window->title_ = title;
	window->title_width_ = 100; // MeasureString(title);
	window->is_dialog_ = true;
	window->texture_id_ = 0;

	// Window can't be smaller than the title, or larger than the screen.
	window->width_ = std::min(
		std::max(width, window->title_width_), GetScreenWidth() - 2);

	window->height_ = std::min(
		height,
		GetScreenHeight() - WINDOW_TITLE_HEIGHT - 3);


	// Center the new dialog on the screen.
	window->x_ = std::max((GetScreenWidth() - window->width_)
		/ 2 - SPLIT_BORDER_WIDTH, 0);
	window->y_ = std::max((GetScreenHeight() - window->height_)
		/ 2 - 2 - WINDOW_TITLE_HEIGHT, 0);

	// Add it to the linked list of dialogs.
	if (first_dialog == nullptr) {
		window->previous_ = nullptr;
		window->next_ = nullptr;
		first_dialog = window;
		last_dialog = window;
	} else {
		first_dialog->previous_ = window;
		window->previous_ = nullptr;
		window->next_ = first_dialog;
		first_dialog = window;
	}

	// Focus on it.
	focused_window = window;

	InvalidateScreen(window->x_, window->y_,
		window->x_ + window->width_ + DIALOG_BORDER_WIDTH + DIALOG_SHADOW_WIDTH,
		window->y_ + window->height_ + DIALOG_BORDER_HEIGHT + DIALOG_SHADOW_WIDTH);
	return window;
}

Window* Window::CreateWindow(std::string_view title) {
	Window* window = new Window();
	window->title_ = title;
	window->title_width_ = 100; // MeasureString(title);
	window->is_dialog_ = false;
	window->texture_id_ = 0;

	Frame::AddWindowToLastFocusedFrame(*window);

	// Focus on it.
	focused_window = window;

	return window;
}

void Window::Focus() {
	if (focused_window == this)
		return;

	if (focused_window) {
		if (focused_window->is_dialog_) {
			focused_window->InvalidateDialogAndTitle();
		} else {
			focused_window->frame_->Invalidate();
		}
	}

	if (is_dialog_) {
		focused_window = this;

		// Move us to the front of the linked list, if we're not already.
		if (previous_ != nullptr) {
			// Remove from current position.
			if (next_) {
				next_->previous_ = previous_;
			} else {
				last_dialog = previous_;
			}
			previous_->next_ = next_;

			// Insert us at the front.
			next_ = first_dialog;
			first_dialog->previous_ = this;
			previous_ = nullptr;
			first_dialog = this;
		}

		InvalidateDialogAndTitle();
	} else {
		frame_->DockFrame.focused_window_ = this;
		focused_window = this;
		frame_->Invalidate();
	}
}

bool Window::IsFocused() {
	return focused_window == this;
}

void Window::Resized() {
}

void Window::Close() {
	int min_x, min_y, max_x, max_y;

	/* find the next window to focus, and remove us */
	if(is_dialog_) {
		min_x = x_;
		min_y = y_;
		max_x = x_ + width_ + DIALOG_BORDER_WIDTH + DIALOG_SHADOW_WIDTH;
		max_y = y_ + height_ + DIALOG_BORDER_HEIGHT + DIALOG_SHADOW_WIDTH;

		if(this == focused_window && next_)
			next_->Focus();

		if(next_)
			next_->previous_ = previous_;
		else
			last_dialog = previous_;

		if(previous_)
			previous_->next_ = next_;
		else
			first_dialog = next_;
	} else {
		/* invalidate this frame */
		min_x = frame_->x_;
		min_y = frame_->y_;
		max_x = frame_->x_ + frame_->width_;
		max_y = frame_->y_ + frame_->height_;

		/* unfocus this window */
		if(this == focused_window) {
			if(next_) /* next window in this frame to focus on? */
				next_->Focus();
			else if(previous_) /* previous window? */
				previous_->Focus();
			else /* unfocus everything */
				UnfocusAllWindows();
		}

		frame_->RemoveWindow(*this);
	}

	if(this == dragging_window)
		dragging_window = nullptr;

	/* todo: free the memory buffer */

	/* todo: notify the process their application has closed */

	delete this;

	InvalidateScreen(min_x, min_y, max_x, max_y);
}

void Window::UnfocusAllWindows() {
}

bool Window::ForEachFrontToBackDialog(const std::function<bool(Window&)>& on_each_dialog) {
	Window* dialog = first_dialog;
	while (dialog != nullptr) {	
		if (on_each_dialog(*dialog))
			return true;

		dialog = dialog->next_;
	}
	return false;
}

void Window::ForEachBackToFrontDialog(const std::function<void(Window&)>& on_each_dialog) {
	Window* dialog = last_dialog;
	while (dialog != nullptr) {
		on_each_dialog(*dialog);
		dialog = dialog->previous_;
	}
}

Window* Window::GetWindowBeingDragged() {
	return dragging_window;
}

void Window::DraggedTo(int screen_x, int screen_y) {
	if (dragging_window != this)
		return;

	if (is_dialog_) {
		int old_x = x_;
		int old_y = y_;

		x_ = screen_x - dragging_offset_x;
		y_ = screen_y - dragging_offset_y;

		// Invalid window because we moved.
		if (old_x != x_ || old_y != y_) {
			InvalidateScreen(std::min(old_x, x_),
				std::min(old_y, y_),
				std::max(old_x, x_) + width_ + DIALOG_BORDER_WIDTH + DIALOG_SHADOW_WIDTH,
				std::max(old_y, y_) + height_ + DIALOG_BORDER_HEIGHT + DIALOG_SHADOW_WIDTH);
		}
	} else {
		// Dragging a tabbed frame.
		if (screen_x >= dragging_offset_x &&
			screen_y >= dragging_offset_y &&
			screen_x <= dragging_offset_x + title_width_ + 2 &&
			screen_y <= dragging_offset_y + WINDOW_TITLE_HEIGHT + 2) {
			// Over the original tab.
			DisableHighlighter();
			return;
		}

		int drop_min_x, drop_min_y, drop_max_x, drop_max_y;
		Frame* drop_frame = Frame::GetDropFrame(*this,
			screen_x, screen_y,
			drop_min_x, drop_min_y,
			drop_max_x, drop_max_y);

		if (drop_frame) {
			// There is somewhere we can drop this window.
			SetHighlighter(drop_min_x, drop_min_y,
				drop_max_x, drop_max_y);
		} else {
			DisableHighlighter();
		}
	}
}

void Window::DroppedAt(int screen_x, int screen_y) {
	if (!is_dialog_) {
		// Dragging a tabbed frame.
		if (screen_x >= dragging_offset_x &&
			screen_y >= dragging_offset_y &&
			screen_x <= dragging_offset_x + title_width_ + 2 &&
			screen_y <= dragging_offset_y + WINDOW_TITLE_HEIGHT + 2) {
			// Over the original tab.
			DisableHighlighter();
			return;
		}
		Frame::DropInWindow(*this, screen_x, screen_y);
		DisableHighlighter();
	}
	dragging_window = nullptr;
}

bool Window::MouseButtonEvent(int screen_x, int screen_y,
	MouseButton button,
		bool is_button_down) {
	if(x_ >= screen_x || y_ >= screen_y ||
		x_ + width_ + DIALOG_BORDER_WIDTH < screen_x ||
		y_ + height_ + DIALOG_BORDER_HEIGHT < screen_y) {
		return false;
	}

	if (screen_y < y_ + WINDOW_TITLE_HEIGHT + 2) {
		// In the title area.
		if (screen_x >= x_ + title_width_ + 2) {
			// But outside of our title tab.
			return false;
		}

		if (button == MouseButton::Left && is_button_down) {
			// We're starting to drag the window.
			dragging_window = this;
			dragging_offset_x = screen_x - x_;
			dragging_offset_y = screen_y - y_;


			// TODO: Test if we closed the window.
		}
	} else {
		// Test if we're clicking in the window's contents.
		int local_x = screen_x - x_ - 1;
		int local_y = screen_y - y_ - WINDOW_TITLE_HEIGHT - 2;
		if (local_x >= 0 && local_y >= 0 &&
			local_x < width_ && local_y < height_) {
			std::cout << "Clicked window at " <<
				local_x << "," << local_y << std::endl;
		}
	}

	// We're in focus!
	Focus();

	// Handle mouse down in this window.
	return true;
}

void Window::HandleTabClick(int offset_along_tab,
		int original_tab_x, int original_tab_y) {
	// TODO: Test if we closed the window.
	dragging_window = this;
	dragging_offset_x = original_tab_x;
	dragging_offset_y = original_tab_y;

	// We're in focus!
	Focus();
}

void Window::Draw(int min_x, int min_y, int max_x, int max_y) {
	// Skip this window if it's out of the redraw region.
	if(x_ >= max_x || y_ >= max_y ||
		x_ + width_ + DIALOG_BORDER_WIDTH + DIALOG_SHADOW_WIDTH < min_x ||
		y_ + height_ + DIALOG_BORDER_HEIGHT + DIALOG_SHADOW_WIDTH < min_y) {
		return;
	}

	int x = x_;
	int y = y_;

	/* draw the left border */
	DrawYLine(x, y, WINDOW_TITLE_HEIGHT + height_ + 3, DIALOG_BORDER_COLOUR, GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());

	/* draw the borders around the top frame */
	DrawXLine(x, y, title_width_ + 2, DIALOG_BORDER_COLOUR, GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());
	DrawYLine(x + title_width_ + 1, y, WINDOW_TITLE_HEIGHT + 1, DIALOG_BORDER_COLOUR,
		GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());

	/* draw shadows */
	DrawYLineAlpha(x + title_width_ + 2, y + 1, WINDOW_TITLE_HEIGHT, DIALOG_SHADOW_0,
		GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());
	DrawYLineAlpha(x + title_width_ + 3, y + 2, WINDOW_TITLE_HEIGHT - 1, DIALOG_SHADOW_1,
		GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());

	/* fill in the colour behind it */
	DrawHeaderBackground(x + 1, y + 1, title_width_,
		focused_window==this? FOCUSED_WINDOW_COLOUR : UNFOCUSED_WINDOW_COLOUR);

	/* write the title */
//	draw_string(x + 2, y + 3, title_, title_length_, WINDOW_TITLE_TEXT_COLOUR,
//		GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());

	/* draw the close button */
//	if(focused_window == this)
//		draw_string(x + title_width_ - 8, y + 3, "X", 1, WINDOW_CLOSE_BUTTON_COLOUR,
//			GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());

	y += WINDOW_TITLE_HEIGHT + 1;

	/* draw the rest of the borders */
	DrawXLine(x + 1, y, width_, DIALOG_BORDER_COLOUR, GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());
	DrawXLine(x + 1, y + height_ + 1, width_, DIALOG_BORDER_COLOUR, GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());
	DrawYLine(x + width_ + 1, y, height_ + 2, DIALOG_BORDER_COLOUR, GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());

	/* draw shadows */
	DrawXLineAlpha(x + 2, y + height_ + 2, width_ + 1, DIALOG_SHADOW_0, GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());
	DrawXLineAlpha(x + 3, y + height_ + 3, width_ + 1, DIALOG_SHADOW_1, GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());
	DrawYLineAlpha(x + width_ + 2, y + 1, height_ + 1, DIALOG_SHADOW_0, GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());
	DrawYLineAlpha(x + width_ + 3, y + 2, height_ + 1, DIALOG_SHADOW_1, GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());
			
	/* draw the contents */
	DrawWindowContents(x + 1, y + 1, min_x, min_y, max_x, max_y);
}

void Window::InvalidateDialogAndTitle() {
	InvalidateScreen(x_, y_, x_ + width_ + DIALOG_BORDER_WIDTH,
		y_ + height_ + DIALOG_BORDER_HEIGHT);
}


void Window::DrawHeaderBackground(int x, int y, int width, uint32 color) {
	uint32 outer_line = color - 0x10101000;
	DrawXLine(x, y, width, outer_line, GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());
	FillRectangle(x, y + 1, width + x, WINDOW_TITLE_HEIGHT + y - 1, color, GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());
	DrawXLine(x, y + WINDOW_TITLE_HEIGHT - 1 , width, outer_line, GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());

}

void Window::DrawWindowContents(int x, int y, int min_x, int min_y, int max_x, int max_y) {
	int draw_min_x = std::max(x, min_x);
	int draw_min_y = std::max(y, min_y);
	int draw_max_x = std::min(x + width_, max_x);
	int draw_max_y = std::min(y + height_, max_y);
	FillRectangle(draw_min_x, draw_min_y,
		draw_max_x, draw_max_y,
		WINDOW_NO_CONTENTS_COLOUR,
		GetWindowManagerTextureData(),
		GetScreenWidth(), GetScreenHeight());
}

void InitializeWindows() {
	dragging_window = nullptr;
	focused_window = nullptr;
	first_dialog = nullptr;
}