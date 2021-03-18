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

#include <map>

#include "compositor.h"
#include "frame.h"
#include "highlighter.h"
#include "perception/draw.h"
#include "perception/font.h"
#include "screen.h"
#include "permebuf/Libraries/perception/devices/keyboard_driver.permebuf.h"

using ::perception::DrawXLine;
using ::perception::DrawXLineAlpha;
using ::perception::DrawYLine;
using ::perception::DrawYLineAlpha;
using ::perception::FillRectangle;
using ::perception::Font;
using ::perception::FontFace;
using ::permebuf::perception::devices::KeyboardDriver;
using ::permebuf::perception::devices::KeyboardListener;
using ::permebuf::perception::devices::MouseButton;
using ::permebuf::perception::devices::MouseListener;

namespace {

constexpr int kMaxTitleLength = 50;

// Font to use for window title.
Font* window_title_font;

// The window being dragged.
Window* dragging_window;

// The currently focused window.
Window* focused_window;

// Linked list of dialogs.
Window* first_dialog;
Window* last_dialog;

// Window that the mouse is currently over the contents of.
Window* hovered_window;

// When dragging a dialog: offset
// When dragging a window: top left of the original title
int dragging_offset_x;
int dragging_offset_y;

std::map<::permebuf::perception::Window, Window*> windows_by_service;

}


Window* Window::CreateDialog(std::string_view title, int width, int height,
		uint32 background_color,
		::permebuf::perception::Window window_listener,
		KeyboardListener keyboard_listener,
		MouseListener mouse_listener) {
	if (window_listener.GetProcessId() == 0 ||
		windows_by_service.count(window_listener) > 0) {
		// Window already exists or a window listener wasn't specified.
		return nullptr;
	}
	if (title.size() > kMaxTitleLength) {
		title = title.substr(0, kMaxTitleLength);
	}
	Window* window = new Window();
	window->title_ = title;
	window->title_width_ = window_title_font->MeasureString(title) +
		WINDOW_TITLE_WIDTH_PADDING;
	window->is_dialog_ = true;
	window->texture_id_ = 0;
	window->fill_color_ = background_color;
	window->window_listener_ = window_listener;
	window->keyboard_listener_ = keyboard_listener;
	window->mouse_listener_ = mouse_listener;

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
	window->Focus();

	windows_by_service[window_listener] = window;
	return window;
}

Window* Window::CreateWindow(std::string_view title,
		uint32 background_color,
		::permebuf::perception::Window window_listener,
		::permebuf::perception::devices::KeyboardListener keyboard_listener,
		::permebuf::perception::devices::MouseListener mouse_listener) {
	if (!window_listener ||
		windows_by_service.count(window_listener) > 0) {
		// Window already exists or a window listener wasn't specified.
		return nullptr;
	}

	if (title.size() > kMaxTitleLength) {
		title = title.substr(0, kMaxTitleLength);
	}

	Window* window = new Window();
	window->title_ = title;
	window->title_width_ = window_title_font->MeasureString(title) +
		WINDOW_TITLE_WIDTH_PADDING;
	window->is_dialog_ = false;
	window->texture_id_ = 0;
	window->fill_color_ = background_color;
	window->keyboard_listener_ = keyboard_listener;
	window->mouse_listener_ = mouse_listener;

	Frame::AddWindowToLastFocusedFrame(*window);

	// Set the window listener after we add the window to the frame, so we
	// don't issue a resize message during creation.
	window->window_listener_ = window_listener;

	// Focus on it.
	focused_window = window;

	windows_by_service[window_listener] = window;
	return window;
}

Window* Window::GetWindow(
	const ::permebuf::perception::Window& window_listener) {
	auto window_itr = windows_by_service.find(window_listener);
	if (window_itr == windows_by_service.end())
		return nullptr;
	return window_itr->second;
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

		// Tell the old window they lost focus.
		if (focused_window->window_listener_) {
			focused_window->window_listener_.SendLostFocus(
				::permebuf::perception::Window::LostFocusMessage());
		}
	}

	if (is_dialog_) {
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
		frame_->Invalidate();
	}
	focused_window = this;

	if (window_listener_) {
		window_listener_.SendGainedFocus(
			::permebuf::perception::Window::GainedFocusMessage());
	}

	// We now want to send keyboard events to this window.
	KeyboardDriver::SetKeyboardListenerMessage keyboard_listener_message;
	keyboard_listener_message.SetNewListener(keyboard_listener_);

	KeyboardDriver::Get().SendSetKeyboardListener(
		keyboard_listener_message);
}

bool Window::IsFocused() {
	return focused_window == this;
}

void Window::Resized() {
	if (window_listener_) {
		::permebuf::perception::Window::SetSizeMessage message;
		message.SetWidth(width_);
		message.SetHeight(height_);
		window_listener_.SendSetSize(message);
	}
}

void Window::Close() {
	int min_x, min_y, max_x, max_y;

	/* find the next window to focus, and remove us */
	if(is_dialog_) {
		min_x = x_;
		min_y = y_;
		max_x = x_ + width_ + DIALOG_BORDER_WIDTH + DIALOG_SHADOW_WIDTH;
		max_y = y_ + height_ + DIALOG_BORDER_HEIGHT + DIALOG_SHADOW_WIDTH;

		if(this == focused_window) {
			if (next_)
				next_->Focus();
			else
				UnfocusAllWindows();
		}

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

	if(this == hovered_window)
		hovered_window = nullptr;

	if (window_listener_) {
		window_listener_.SendClosed(
			::permebuf::perception::Window::ClosedMessage());
	}

	windows_by_service.erase(window_listener_);

	delete this;

	InvalidateScreen(min_x, min_y, max_x, max_y);
}

void Window::UnfocusAllWindows() {
	if (focused_window && focused_window->window_listener_) {
			focused_window->window_listener_.SendLostFocus(
				::permebuf::perception::Window::LostFocusMessage());
	}
	KeyboardDriver::Get().SendSetKeyboardListener(
		KeyboardDriver::SetKeyboardListenerMessage());
}

bool Window::ForEachFrontToBackDialog(
	const std::function<bool(Window&)>& on_each_dialog) {
	Window* dialog = first_dialog;
	while (dialog != nullptr) {
		if (on_each_dialog(*dialog))
			return true;

		dialog = dialog->next_;
	}
	return false;
}

void Window::ForEachBackToFrontDialog(
	const std::function<void(Window&)>& on_each_dialog) {
	Window* dialog = last_dialog;
	while (dialog != nullptr) {
		on_each_dialog(*dialog);
		dialog = dialog->previous_;
	}
}

Window* Window::GetWindowBeingDragged() {
	return dragging_window;
}

void Window::MouseNotHoveringOverWindowContents() {
	if (hovered_window != nullptr) {
		if (hovered_window->mouse_listener_) {
			hovered_window->mouse_listener_.SendOnMouseLeave(
				MouseListener::OnMouseLeaveMessage());
		}
		hovered_window = nullptr;
	}
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
				std::max(old_x, x_) + width_ +
					DIALOG_BORDER_WIDTH + DIALOG_SHADOW_WIDTH,
				std::max(old_y, y_) + height_ +
					DIALOG_BORDER_HEIGHT + DIALOG_SHADOW_WIDTH);
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
	dragging_window = nullptr;

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
}

bool Window::MouseEvent(int screen_x, int screen_y,
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

		// We're over the title and not the contents.
		MouseNotHoveringOverWindowContents();

		if (button == MouseButton::Left && is_button_down) {
			if (IsFocused() && screen_x >=
				x_ + title_width_ - 1 - WINDOW_TITLE_WIDTH_PADDING) {
				// We clicked the close button.
				Close();
				std::cout << "Window closed" << std::endl;
				return true;
			}
			// We're starting to drag the window.
			dragging_window = this;
			dragging_offset_x = screen_x - x_;
			dragging_offset_y = screen_y - y_;
		}
	} else {
		// Test if we're clicking in the window's contents.
		int local_x = screen_x - x_ - 1;
		int local_y = screen_y - y_ - WINDOW_TITLE_HEIGHT - 2;
		if (local_x >= 0 && local_y >= 0 &&
			local_x < width_ && local_y < height_) {
			// We're over the contents.
			if (hovered_window != this) {
				// We just entered this window.
				if (hovered_window != nullptr &&
					hovered_window->mouse_listener_) {
					// Let the old window we were hovering over know the mouse has
					// left the premises.
					hovered_window->mouse_listener_.SendOnMouseLeave(
						MouseListener::OnMouseLeaveMessage());
				}
				hovered_window = this;
				if (window_listener_) {
					// Let our window know the mouse has entered the premises.
					mouse_listener_.SendOnMouseEnter(
						MouseListener::OnMouseEnterMessage());
				}
			}

			if (mouse_listener_) {
				if (button == MouseButton::Unknown) {
					// We were hovered over.
					MouseListener::OnMouseHoverMessage message;
					message.SetX(local_x);
					message.SetY(local_y);
					mouse_listener_.SendOnMouseHover(message);
				} else {
					// We were clicked.
					MouseListener::OnMouseClickMessage message;
					message.SetButton(button);
					message.SetX(local_x);
					message.SetY(local_y);
					message.SetWasPressedDown(is_button_down);
					mouse_listener_.SendOnMouseClick(message);
				}
			}
		} else {
			// We're over the frame but not the contents.
			MouseNotHoveringOverWindowContents();
		}
	}

	if (button != MouseButton::Unknown) {
		// We're in focus!
		Focus();
	}

	// Handle mouse in this window.
	return true;
}

void Window::HandleTabClick(int offset_along_tab,
		int original_tab_x, int original_tab_y) {
	if (IsFocused() && offset_along_tab >=
		title_width_ - WINDOW_TITLE_WIDTH_PADDING) {
		// We clicked the close button.
		Close();
		return;
	}
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

	DrawDecorations(min_x, min_y, max_x, max_y);
	// Draw the contents of the window.
	DrawWindowContents(x_ + 1, y_ + WINDOW_TITLE_HEIGHT + 2, min_x, min_y, max_x,
		max_y);
}

void Window::InvalidateDialogAndTitle() {
	InvalidateScreen(x_, y_, x_ + width_ + DIALOG_BORDER_WIDTH,
		y_ + height_ + DIALOG_BORDER_HEIGHT);
}

void Window::InvalidateContents(int min_x, int min_y, int max_x, int max_y) {
	max_x = std::min(max_x, width_);
	max_y = std::min(max_y, height_);
	int x = x_ + 2;
	int y = y_ + WINDOW_TITLE_HEIGHT + 2;
	InvalidateScreen(x + min_x, y + min_y, x + max_x, y + max_y);
}

void Window::SetTextureId(int texture_id) {
	texture_id_ = texture_id;
}

void Window::DrawHeaderBackground(int x, int y, int width, uint32 color) {
	uint32 outer_line = color - 0x10101000;
	DrawXLine(x, y, width, outer_line, GetWindowManagerTextureData(),
		GetScreenWidth(), GetScreenHeight());
	FillRectangle(x, y + 1, width + x, WINDOW_TITLE_HEIGHT + y - 1, color,
		GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());
	DrawXLine(x, y + WINDOW_TITLE_HEIGHT - 1 , width, outer_line,
		GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());
}


void Window::DrawDecorations(int min_x, int min_y, int max_x, int max_y) {
/*	if (min_x >= x_ + 1 && min_y >= y_ + WINDOW_TITLE_HEIGHT + 2 &&
		max_x <= x_ + width_ + 1 &&
		max_y <= y_ + height_ + WINDOW_TITLE_HEIGHT + 2) {
		// The draw region only included the window's contents, so we can skip
		// drawing the window's frame.
		return;
	}*/

	int x = x_;
	int y = y_;

	/* draw the left border */
	DrawYLine(x, y, WINDOW_TITLE_HEIGHT + height_ + 3, DIALOG_BORDER_COLOUR,
		GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());

	// Is the header in our draw region?
//	if (max_x > x_ + 1 && max_y > y_ + 1 &&
//		min_x <= x_ + title_width_ + 2 &&
//		min_y <= y_ + WINDOW_TITLE_HEIGHT + 2) {
		/* draw the borders around the top frame */
		DrawXLine(x, y, title_width_ + 2, DIALOG_BORDER_COLOUR,
			GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());
		DrawYLine(x + title_width_ + 1, y, WINDOW_TITLE_HEIGHT + 1,
			DIALOG_BORDER_COLOUR, GetWindowManagerTextureData(),
			GetScreenWidth(), GetScreenHeight());

		/* fill in the colour behind it */
		DrawHeaderBackground(x + 1, y + 1, title_width_,
			focused_window==this? FOCUSED_WINDOW_COLOUR : UNFOCUSED_WINDOW_COLOUR);

		/* write the title */
		window_title_font->DrawString(x + 2, y + 3, title_,
			WINDOW_TITLE_TEXT_COLOUR, GetWindowManagerTextureData(),
			GetScreenWidth(), GetScreenHeight());

		/* draw the close button */
		if(focused_window == this) {
			window_title_font->DrawString(x + title_width_ - 8, y + 3, "X",
				WINDOW_CLOSE_BUTTON_COLOUR, GetWindowManagerTextureData(),
				GetScreenWidth(), GetScreenHeight());
		}

		CopyTexture(std::max(x_, min_x), std::max(y, min_y),
			std::min(x_ + title_width_ + 2, max_x),
			std::min(y + WINDOW_TITLE_HEIGHT + 1, max_y),
			GetWindowManagerTextureId(),
			std::max(x_, min_x), std::max(y, min_y));
//	}

	y += WINDOW_TITLE_HEIGHT + 1;

	/* draw the rest of the borders */
	DrawXLine(x + 1, y, width_, DIALOG_BORDER_COLOUR,
		GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());
	DrawXLine(x + 1, y + height_ + 1, width_, DIALOG_BORDER_COLOUR,
		GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());
	DrawYLine(x + width_ + 1, y, height_ + 2, DIALOG_BORDER_COLOUR,
		GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());

	CopyTexture(std::max(x_, min_x), std::max(y, min_y),
		std::min(x_ + width_ + 2, max_x), std::min(y + height_ + 2, max_y),
		GetWindowManagerTextureId(),
		std::max(x_, min_x), std::max(y, min_y));

}

void Window::DrawWindowContents(
	int x, int y, int min_x, int min_y, int max_x, int max_y) {
	int draw_min_x = std::max(x, min_x);
	int draw_min_y = std::max(y, min_y);
	int draw_max_x = std::min(x + width_, max_x);
	int draw_max_y = std::min(y + height_, max_y);

	if (texture_id_ == 0) {
		DrawSolidColor(draw_min_x, draw_min_y, draw_max_x, draw_max_y,
			fill_color_);
	} else {
		CopyTexture(draw_min_x, draw_min_y, draw_max_x, draw_max_y,
			texture_id_,
			draw_min_x - x, draw_min_y - y);
	}
}

void InitializeWindows() {
	dragging_window = nullptr;
	focused_window = nullptr;
	first_dialog = nullptr;
	hovered_window = nullptr;
	window_title_font = Font::LoadFont(FontFace::DejaVuSans);
}

Font* GetWindowTitleFont() {
	return window_title_font;
}