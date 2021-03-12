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

#include "frame.h"

#include "compositor.h"
#include "highlighter.h"
#include "perception/draw.h"
#include "perception/font.h"
#include "screen.h"
#include "window.h"

using ::perception::DrawXLine;
using ::perception::DrawXLineAlpha;
using ::perception::DrawYLine;
using ::perception::DrawYLineAlpha;
using ::perception::FillRectangle;
using ::perception::Font;
using ::permebuf::perception::devices::MouseButton;

namespace {

// Top level frame.
Frame* root_frame;

// The last focused frame, for figuring out where to open the next window.
Frame* last_focused_frame;

Frame* dragging_frame;

int frame_dragging_offset;

}

Frame* Frame::GetRootFrame() {
	return root_frame;
}

Frame* Frame::GetFrameBeingDragged() {
	return dragging_frame;
}

void Frame::DraggedTo(int screen_x, int screen_y) {
	if (SplitFrame.is_split_vertically_) {
		screen_y -= frame_dragging_offset;
		if (screen_y > y_ && y_ < screen_y + height_ - SPLIT_BORDER_WIDTH &&
			SplitFrame.child_a_->IsValidSize(width_, screen_y - y_) &&
			SplitFrame.child_b_->IsValidSize(width_,
				y_ + height_ - screen_y - SPLIT_BORDER_WIDTH)) {
			SetHighlighter(x_, screen_y,
				x_ + width_, screen_y + SPLIT_BORDER_WIDTH);
		} else {
			DisableHighlighter();
		}
	} else {
		screen_x -= frame_dragging_offset;
		if (screen_x > x_ && x_ < screen_x + width_ - SPLIT_BORDER_WIDTH &&
			SplitFrame.child_a_->IsValidSize(screen_x - x_, height_) &&
			SplitFrame.child_b_->IsValidSize(
				x_ + width_ - screen_x - SPLIT_BORDER_WIDTH, height_)) {
			SetHighlighter(screen_x, y_,
				screen_x + SPLIT_BORDER_WIDTH, y_ + height_);
		} else {
			DisableHighlighter();
		}
	}
}

void Frame::DroppedAt(int screen_x, int screen_y) {
	dragging_frame = nullptr;
	DisableHighlighter();

if (SplitFrame.is_split_vertically_) {
		screen_y -= frame_dragging_offset;
		if (screen_y > y_ && y_ < screen_y + height_ - SPLIT_BORDER_WIDTH &&
			SplitFrame.child_a_->IsValidSize(width_, screen_y - y_) &&
			SplitFrame.child_b_->IsValidSize(width_,
				y_ + height_ - screen_y - SPLIT_BORDER_WIDTH)) {
			SplitFrame.split_percent_ = (float)(screen_y - y_) /
				(float)height_;
			UpdateFrame(true);
		}
	} else {
		screen_x -= frame_dragging_offset;
		if (screen_x > x_ && x_ < screen_x + width_ - SPLIT_BORDER_WIDTH &&
			SplitFrame.child_a_->IsValidSize(screen_x - x_, height_) &&
			SplitFrame.child_b_->IsValidSize(
				x_ + width_ - screen_x - SPLIT_BORDER_WIDTH, height_)) {
			SplitFrame.split_percent_ = (float)(screen_x - x_) /
				(float)width_;
			UpdateFrame(true);
		}
	}
}

Frame* Frame::GetDropFrame(
	const Window& window, int mouse_x, int mouse_y,
	int& min_x, int& min_y, int& max_x, int& max_y) {
	if(!root_frame) return nullptr;

	struct Frame *current_frame = root_frame;

	while(current_frame != nullptr) {
		// Is this already a split frame?
		if(current_frame->is_split_frame_) {
			if(current_frame->SplitFrame.is_split_vertically_) {
				if(mouse_y < current_frame->y_ +
					current_frame->SplitFrame.split_point_)
					current_frame = current_frame->SplitFrame.child_a_;
				else if(mouse_y > current_frame->y_ +
					current_frame->SplitFrame.split_point_)
					current_frame = current_frame->SplitFrame.child_b_;
				else
					return nullptr; // On the border

			} else {
				// Could we deal with being split vertically? will our title fit
				// in the new frame?
				if(mouse_x < current_frame->x_ +
					current_frame->SplitFrame.split_point_) {
					// Can our title fit in this frame?
					if(window.title_width_ + 2 <
						current_frame->SplitFrame.child_a_->width_)
						current_frame = current_frame->SplitFrame.child_a_;
					else
						return nullptr;
				} else if(mouse_x > current_frame->y_ +
					current_frame->SplitFrame.split_point_) {
					if(window.title_width_ + 2 < 
						current_frame->SplitFrame.child_b_->width_)
						current_frame = current_frame->SplitFrame.child_b_;
					else
						return nullptr;
				} else
					return nullptr; // On the border.
			}
		} else {
			if (current_frame->DockFrame.first_window_ != &window || 
				current_frame->DockFrame.last_window_ != &window) {
				// We can split this dock if the window we're trying to put here
				// isn't this frame's only child.
				int q_width = current_frame->width_ / 4;
				int q_height = current_frame->height_ / 4;

				// Split the top.
				if(mouse_x > q_width + current_frame->x_ &&
					mouse_x < current_frame->x_ +
						current_frame->width_ -q_width &&
					mouse_y < current_frame->y_ + q_height &&
					current_frame->height_ > WINDOW_MINIMUM_HEIGHT) {

					min_x = current_frame->x_;
					min_y = current_frame->y_;
					max_x = current_frame->x_ + current_frame->width_;
					max_y = current_frame->y_ + current_frame->height_ / 2;

					return current_frame;
				}

				// Split the bottom.
				if(mouse_x > q_width + current_frame->x_ &&
					mouse_x < current_frame->x_ +
						current_frame->width_ - q_width &&
					mouse_y > current_frame->y_ +
						current_frame->height_ - q_height &&
					current_frame->height_ > WINDOW_MINIMUM_HEIGHT) {

					min_x = current_frame->x_;
					min_y = current_frame->y_ + current_frame->height_ / 2 +
						SPLIT_BORDER_WIDTH;
					max_x = current_frame->x_ + current_frame->width_;
					max_y = current_frame->y_ + current_frame->height_;

					return current_frame;
				}

				// Could we deal with being split horizontally? Will our title
				// fit in the new frame?
				bool can_split_horizontally =
					(window.title_width_ + 2) < current_frame->width_ / 2 -
						SPLIT_BORDER_WIDTH &&
					current_frame->LongestWindowTitleWidth() + 2 <
						current_frame->width_ / 2 - SPLIT_BORDER_WIDTH;

				if(can_split_horizontally) {
					// Split the left.
					if(mouse_y > q_height + current_frame->y_ &&
						mouse_y < current_frame->y_ + current_frame->height_ -
							q_height &&
						mouse_x < current_frame->x_ + q_width) {
					
						min_x = current_frame->x_;
						min_y = current_frame->y_;
						max_x = current_frame->x_ + current_frame->width_ / 2;
						max_y = current_frame->y_ + current_frame->height_;
						return current_frame;
					}

					// Split the right.
					if(mouse_y > q_height + current_frame->y_ &&
						mouse_y < current_frame->y_ + current_frame->height_ -
							q_height &&
						mouse_x > current_frame->x_ + current_frame->width_ -
							q_width) {
					
						min_x = current_frame->x_ + current_frame->width_ / 2 +
							SPLIT_BORDER_WIDTH;
						min_y = current_frame->y_;
						max_x = current_frame->x_ + current_frame->width_;
						max_y = current_frame->y_ + current_frame->height_;
						return current_frame;
					}
				}
			}

			// Drop here.
			min_x = current_frame->x_;
			min_y = current_frame->y_;
			max_x = current_frame->x_ + current_frame->width_;
			max_y = current_frame->y_ + current_frame->height_;

			return current_frame;

		}
	}

	return nullptr;
}


void Frame::DropInWindow(
	Window& window, int mouse_x, int mouse_y) {
	int drop_min_x, drop_min_y, drop_max_x, drop_max_y;
	Frame* drop_frame = Frame::GetDropFrame(window,
		mouse_x, mouse_y,
		drop_min_x, drop_min_y,
		drop_max_x, drop_max_y);
	if (!drop_frame) {
		// No where to drop us.
		return;
	}

	if(drop_min_x == drop_frame->x_ &&
		drop_min_y == drop_frame->y_ &&
		drop_max_x == drop_frame->x_ + drop_frame->width_ &&
		drop_max_y == drop_frame->y_ + drop_frame->height_) {
		/* add it to this window */

		if(window.frame_ != drop_frame) {
			/* not already part of this window */
			window.frame_->RemoveWindow(window);
			drop_frame->AddWindow(window);
			last_focused_frame = drop_frame;
		}
	} else if(drop_max_x != drop_frame->x_ + drop_frame->width_) {
		/* drop left */
		/* create a left and right frame */
		Frame* child_a = new Frame();
		Frame* child_b = new Frame();

		window.frame_->RemoveWindow(window);

		/* create two child frames */
		child_a->x_ = drop_frame->x_;
		child_a->y_ = drop_frame->y_;
		child_a->parent_ = drop_frame;
		child_a->width_ = drop_frame->width_ / 2;
		child_a->height_ = drop_frame->height_;
		child_a->is_split_frame_ = false;
		child_a->DockFrame.first_window_ = nullptr;
		child_a->DockFrame.last_window_ = nullptr;
		child_a->DockFrame.focused_window_ = nullptr;

		child_b->x_ = drop_frame->x_ + drop_frame->width_ / 2 +
			SPLIT_BORDER_WIDTH;
		child_b->y_ = drop_frame->y_;
		child_b->parent_ = drop_frame;
		child_b->width_ = drop_frame->width_ / 2 - SPLIT_BORDER_WIDTH;
		child_b->height_ = drop_frame->height_;
		child_b->is_split_frame_ = false;
		child_b->DockFrame.first_window_ =
			drop_frame->DockFrame.first_window_;
		child_b->DockFrame.last_window_ =
			drop_frame->DockFrame.last_window_;
		child_b->DockFrame.focused_window_ =
			drop_frame->DockFrame.focused_window_;

		/* move this frame's children into child_b */
		Window *w = child_b->DockFrame.first_window_;
		while(w) {
			w->frame_ = child_b;
			w = w->next_;
		}

		/* turn this frame into a split frame */
		drop_frame->is_split_frame_ = true;
		drop_frame->SplitFrame.child_a_ = child_a;
		drop_frame->SplitFrame.child_b_ = child_b;
		drop_frame->SplitFrame.is_split_vertically_ = false;
		drop_frame->SplitFrame.split_percent_ = 0.5f;
		drop_frame->SplitFrame.split_point_ = 0; /* makes it update */

		child_a->AddWindow(window);
		last_focused_frame = child_a;

		drop_frame->UpdateFrame(true);
	} else if(drop_min_x != drop_frame->x_) {
		/* drop right */
		/* create a left and right frame */
		Frame* child_a = new Frame();
		Frame* child_b = new Frame();

		window.frame_->RemoveWindow(window);

		/* create two child frames */
		child_a->x_ = drop_frame->x_;
		child_a->y_ = drop_frame->y_;
		child_a->parent_ = drop_frame;
		child_a->width_ = drop_frame->width_ / 2;
		child_a->height_ = drop_frame->height_;
		child_a->is_split_frame_ = false;
		child_a->DockFrame.first_window_ =
			drop_frame->DockFrame.first_window_;
		child_a->DockFrame.last_window_ =
			drop_frame->DockFrame.last_window_;
		child_a->DockFrame.focused_window_ =
			drop_frame->DockFrame.focused_window_;

		child_b->x_ = drop_frame->x_ + drop_frame->width_ / 2 +
			SPLIT_BORDER_WIDTH;
		child_b->y_ = drop_frame->y_;
		child_b->parent_ = drop_frame;
		child_b->width_ = drop_frame->width_ / 2 - SPLIT_BORDER_WIDTH;
		child_b->height_ = drop_frame->height_;
		child_b->is_split_frame_ = false;
		child_b->DockFrame.first_window_ = 0;
		child_b->DockFrame.last_window_ = 0;
		child_b->DockFrame.focused_window_ = 0;

		/* move this frame's children into child_b */
		Window *w = child_a->DockFrame.first_window_;
		while(w) {
			w->frame_ = child_a;
			w = w->next_;
		}

		/* turn this frame into a split frame */
		drop_frame->is_split_frame_ = true;
		drop_frame->SplitFrame.child_a_ = child_a;
		drop_frame->SplitFrame.child_b_ = child_b;
		drop_frame->SplitFrame.is_split_vertically_ = false;
		drop_frame->SplitFrame.split_percent_ = 0.5f;
		drop_frame->SplitFrame.split_point_ = 0; /* makes it update */

		child_b->AddWindow(window);
		last_focused_frame = child_b;

		drop_frame->UpdateFrame(true);
	} else if(drop_max_y != drop_frame->y_ + drop_frame->height_) {
		/* drop top */
		/* create a top and bottom frame */
		Frame* child_a = new Frame();
		Frame* child_b = new Frame();
		window.frame_->RemoveWindow(window);

		/* create two child frames */
		child_a->x_ = drop_frame->x_;
		child_a->y_ = drop_frame->y_;
		child_a->parent_ = drop_frame;
		child_a->width_ = drop_frame->width_;
		child_a->height_ = drop_frame->height_ / 2;
		child_a->is_split_frame_ = false;
		child_a->DockFrame.first_window_ = 0;
		child_a->DockFrame.last_window_ = 0;
		child_a->DockFrame.focused_window_ = 0;

		child_b->x_ = drop_frame->x_;
		child_b->y_ = drop_frame->y_ + drop_frame->height_ / 2 +
			SPLIT_BORDER_WIDTH;
		child_b->parent_ = drop_frame;
		child_b->width_ = drop_frame->width_;
		child_b->height_ = drop_frame->height_ / 2 - SPLIT_BORDER_WIDTH;
		child_b->is_split_frame_ = false;
		child_b->DockFrame.first_window_ =
			drop_frame->DockFrame.first_window_;
		child_b->DockFrame.last_window_ =
			drop_frame->DockFrame.last_window_;
		child_b->DockFrame.focused_window_ =
			drop_frame->DockFrame.focused_window_;

		/* move this frame's children into child_b */
		Window *w = child_b->DockFrame.first_window_;
		while(w) {
			w->frame_ = child_b;
			w = w->next_;
		}

		/* turn this frame into a split frame */
		drop_frame->is_split_frame_ = true;
		drop_frame->SplitFrame.child_a_ = child_a;
		drop_frame->SplitFrame.child_b_ = child_b;
		drop_frame->SplitFrame.is_split_vertically_ = true;
		drop_frame->SplitFrame.split_percent_ = 0.5f;
		drop_frame->SplitFrame.split_point_ = 0; /* makes it update */

		child_a->AddWindow(window);
		last_focused_frame = child_a;

		drop_frame->UpdateFrame(true);

	} else if(drop_min_y != drop_frame->y_) {
		/* drop bottom */
		/* create a top and bottom frame */
		Frame* child_a = new Frame();
		Frame* child_b = new Frame();
		window.frame_->RemoveWindow(window);

		/* create two child frames */
		child_a->x_ = drop_frame->x_;
		child_a->y_ = drop_frame->y_;
		child_a->parent_ = drop_frame;
		child_a->width_ = drop_frame->width_;
		child_a->height_ = drop_frame->height_ / 2;
		child_a->is_split_frame_ = false;
		child_a->DockFrame.first_window_ =
			drop_frame->DockFrame.first_window_;
		child_a->DockFrame.last_window_ =
			drop_frame->DockFrame.last_window_;
		child_a->DockFrame.focused_window_ =
			drop_frame->DockFrame.focused_window_;

		child_b->x_ = drop_frame->x_;
		child_b->y_ = drop_frame->y_ + drop_frame->height_ / 2 +
			SPLIT_BORDER_WIDTH;
		child_b->parent_ = drop_frame;
		child_b->width_ = drop_frame->width_;
		child_b->height_ = drop_frame->height_ / 2 - SPLIT_BORDER_WIDTH;
		child_b->is_split_frame_ = false;
		child_b->DockFrame.first_window_ = 0;
		child_b->DockFrame.last_window_ = 0;
		child_b->DockFrame.focused_window_ = 0;

		/* move this frame's children into child_b */
		Window *w = child_a->DockFrame.first_window_;
		while(w) {
			w->frame_ = child_a;
			w = w->next_;
		}

		/* turn this frame into a split frame */
		drop_frame->is_split_frame_ = true;
		drop_frame->SplitFrame.child_a_ = child_a;
		drop_frame->SplitFrame.child_b_ = child_b;
		drop_frame->SplitFrame.is_split_vertically_ = true;
		drop_frame->SplitFrame.split_percent_ = 0.5f;
		drop_frame->SplitFrame.split_point_ = 0; /* makes it update */

		child_b->AddWindow(window);
		last_focused_frame = child_b;

		drop_frame->UpdateFrame(true);
	}
}


void Frame::UpdateFrame(bool resized) {
	if(is_split_frame_) {
		Frame *replace_me = nullptr; /* child to replace me with if the other child closed */
		if(!SplitFrame.child_a_) /* child a closed, promote child b to my position */
			replace_me = SplitFrame.child_b_;
		else if(!SplitFrame.child_b_) /* child b closed, promote child a to my position */
			replace_me = SplitFrame.child_a_;

		if(replace_me) {
			if(dragging_frame) {
				dragging_frame = nullptr;
				// dragging_temp_maxx = 0;
			}

			replace_me->x_ = x_;
			replace_me->y_ = y_;
			replace_me->width_ = width_;
			replace_me->height_ = height_;
			replace_me->parent_ = parent_;

			// Replace me in the parent
			if(this == root_frame)
				root_frame = replace_me;
			else if(parent_->SplitFrame.child_a_ == this)
				parent_->SplitFrame.child_a_ = replace_me;
			else
				parent_->SplitFrame.child_b_ = replace_me;


			InvalidateScreen(x_, y_,
				x_ + width_, y_ + height_);

			delete this;

			// Update this child.
			replace_me->UpdateFrame(true);

			return;
		}

		if(resized) {
			/* update the split point */
			if(SplitFrame.is_split_vertically_) {
				int split_point = (int)((float)height_ *
					SplitFrame.split_percent_);
				// if(split_point != SplitFrame.split_point) {
					SplitFrame.split_point_ = split_point;
					SplitFrame.child_a_->x_ = x_;
					SplitFrame.child_a_->y_ = y_;
					SplitFrame.child_a_->width_ = width_;
					SplitFrame.child_a_->height_ = split_point;
					SplitFrame.child_a_->UpdateFrame(true);

					SplitFrame.child_b_->x_ = x_;
					SplitFrame.child_b_->y_ = y_ + split_point +
						SPLIT_BORDER_WIDTH;
					SplitFrame.child_b_->width_ = width_;
					SplitFrame.child_b_->height_ = height_ - split_point -
						SPLIT_BORDER_WIDTH;
					SplitFrame.child_b_->UpdateFrame(true);
				//}
			} else {
				int split_point = (int)((float)width_ *
					SplitFrame.split_percent_);
				//if(split_point != SplitFrame.split_point) {
					SplitFrame.split_point_ = split_point;
					SplitFrame.child_a_->x_ = x_;
					SplitFrame.child_a_->y_ = y_;
					SplitFrame.child_a_->width_ = split_point;
					SplitFrame.child_a_->height_ = height_;
					SplitFrame.child_a_->UpdateFrame(true);

					SplitFrame.child_b_->x_ = x_ + split_point +
						SPLIT_BORDER_WIDTH;
					SplitFrame.child_b_->y_ = y_;
					SplitFrame.child_b_->width_ = width_ - split_point -
						SPLIT_BORDER_WIDTH;
					SplitFrame.child_b_->height_ = height_;
					SplitFrame.child_b_->UpdateFrame(true);		
				//}
			}

			InvalidateScreen(x_, y_,
				x_ + width_, y_ + height_);

		}
	} else {
		/* if there's nothing in the frame, delete it */
		if(!DockFrame.first_window_) {
			if(root_frame == this) { /* close the root frame */
				delete this;
				root_frame = nullptr;
				InvalidateScreen(0, 0, GetScreenWidth(), GetScreenHeight());
				return;
			}

			/* remove this from the parent split frame */
			if(parent_->SplitFrame.child_a_ == this)
				parent_->SplitFrame.child_a_ = nullptr;
			else
				parent_->SplitFrame.child_b_ = nullptr;
			
			parent_->UpdateFrame(false);
			delete this;
			return;
		}

		// Update the title height.
		size_t old_title_height = DockFrame.title_height_;
		// Top border and initial row.
		size_t new_title_height = WINDOW_TITLE_HEIGHT + 1;
		// Left border.
		size_t titles_on_this_line = 1;
		struct Window *w = DockFrame.first_window_;
		while(w) {
			if(width_ > titles_on_this_line + w->title_width_ + 1) {
				// Title and right border.
				titles_on_this_line += w->title_width_ + 1;
			}
			else {
				new_title_height += WINDOW_TITLE_HEIGHT + 1;
				titles_on_this_line = 1;
			}

			w = w->next_;
		}

		new_title_height++; /* bottom border */
		size_t new_client_height;
		new_client_height = new_title_height > height_ ? 0 : height_ -
			new_title_height;

		if(new_title_height != old_title_height || resized) {
			// Resize each window if the title height has changed.
			w = DockFrame.first_window_;
			while(w) {
				w->x_ = x_;
				w->y_ = y_ + new_title_height;
				w->width_ = width_;
				w->height_ = new_client_height;
				w->Resized();
				w = w->next_;
			}

			DockFrame.title_height_ = new_title_height;
		}
	}

	InvalidateScreen(x_, y_, width_, height_);
}

void Frame::AddWindow(Window& window) {
	// We are a split frame, add us to our largest side.
	if(is_split_frame_) {
		if(SplitFrame.split_percent_ > 0.5f)
			return SplitFrame.child_a_->AddWindow(window);
		else
			return SplitFrame.child_b_->AddWindow(window);
	}

	// Add them to this frame.
	window.next_ = nullptr;

	if(DockFrame.first_window_) {
		DockFrame.last_window_->next_ = &window;
		window.previous_ = DockFrame.last_window_;
		DockFrame.last_window_ = &window;
	} else {
		DockFrame.first_window_ = &window;
		DockFrame.last_window_ = &window;
		window.previous_ = 0;
		DockFrame.title_height_ = 0;
	}

	DockFrame.focused_window_ = &window;
	window.frame_ = this;

	// Updates the frame's title height.
	UpdateFrame(false);

	window.x_ = x_;
	window.y_ = y_ + DockFrame.title_height_;
	window.width_ = width_;
	window.height_ = height_ - DockFrame.title_height_;
}

void Frame::RemoveWindow(Window& window) {
	/* remove from the frame */
	if(window.next_)
		window.next_->previous_ = window.previous_;
	else
		window.frame_->DockFrame.last_window_ = window.previous_;

	if(window.previous_)
		window.previous_->next_ = window.next_;
	else
		window.frame_->DockFrame.first_window_ = window.next_;

	if(DockFrame.focused_window_ == &window) { /* was our focused window? */
		if(window.next_)
			DockFrame.focused_window_ = window.next_;
		else
			DockFrame.focused_window_ = window.previous_;
	}

	/* invalidate this frame */
	InvalidateScreen(x_, y_,
		x_ + width_, y_ + height_);
	
	UpdateFrame(false);
}

void Frame::AddWindowToLastFocusedFrame(Window& window) {
	if (!last_focused_frame) {
		if (!root_frame) {
			root_frame = new Frame();
			root_frame->x_ = 0;
			root_frame->y_ = 0;
			root_frame->parent_ = nullptr;
			root_frame->width_ = GetScreenWidth();
			root_frame->height_ = GetScreenHeight();
			root_frame->is_split_frame_ = false;
			root_frame->DockFrame.first_window_ = nullptr;
			root_frame->DockFrame.last_window_ = nullptr;
			root_frame->DockFrame.focused_window_ = nullptr;
		}
		last_focused_frame = root_frame;
	}

	last_focused_frame->AddWindow(window);
}


void Frame::MouseEvent(int screen_x, int screen_y,
	::permebuf::perception::devices::MouseButton button,
	bool is_button_down) {
	if (is_split_frame_) {
		// Split frame, see what we clicked.
		if (SplitFrame.is_split_vertically_) {
			if (screen_y < y_ + SplitFrame.split_point_) {
				// Clicked the top frame.
				SplitFrame.child_a_->MouseEvent(
					screen_x, screen_y, button, is_button_down);
			}
			else if (screen_y < y_ + SplitFrame.split_point_ +
				SPLIT_BORDER_WIDTH) {
				// Split point.
				if (button == MouseButton::Left && is_button_down) {
					// Start dragging the split point.
					dragging_frame = this;
					frame_dragging_offset = screen_y - 
						SplitFrame.split_point_ + SPLIT_BORDER_WIDTH;
				}
				Window::MouseNotHoveringOverWindowContents();
				return;
			} else {
				// Clicked the bottom frame.
				SplitFrame.child_b_->MouseEvent(
					screen_x, screen_y, button, is_button_down);
			}
		} else {
			if (screen_x < x_ + SplitFrame.split_point_) {
				// Clicked the left frame.
				SplitFrame.child_a_->MouseEvent(
					screen_x, screen_y, button, is_button_down);
			} else if (screen_x < x_ + SplitFrame.split_point_ +
				SPLIT_BORDER_WIDTH) {
				// Split point.
				if (button == MouseButton::Left && is_button_down) {
					// Start dragging the split point.
					dragging_frame = this;
					frame_dragging_offset = screen_x - 
						SplitFrame.split_point_ + SPLIT_BORDER_WIDTH;
				}
				Window::MouseNotHoveringOverWindowContents();
				return;
			} else {
				// Clicked the right frame.
				SplitFrame.child_b_->MouseEvent(
					screen_x, screen_y, button, is_button_down);
			}
		}
	} else {
		// Dock frame.
		if (screen_y < y_ + DockFrame.title_height_) {
			// Clicked the title.
			Window::MouseNotHoveringOverWindowContents();

			// See who's title we clicked.
			Window* window = DockFrame.first_window_;

			int next_title_y = y_ + WINDOW_TITLE_HEIGHT + 2;
			int title_x = x_ + 1;

			while (window) {
				// Loop while we haven't clicked any and there are still windows
				// to test.
				if (title_x + window->title_width_ + 1 > width_ + x_) {
					// Next line.
					title_x = x_ + 1;
					next_title_y += WINDOW_TITLE_HEIGHT + 1;
				}

				if (screen_y < next_title_y &&
					screen_y >= next_title_y - WINDOW_TITLE_HEIGHT - 1 &&
					screen_x < title_x + window->title_width_ + 1) {
					// We clicked on this window's tab.
					if (button == MouseButton::Left && is_button_down) {
						// Maybe a drag or closing a tab.
						window->HandleTabClick(screen_x - title_x,
							title_x, next_title_y - WINDOW_TITLE_HEIGHT - 1);
					} else if (button != MouseButton::Unknown) {
						// Focus this tab.
						window->Focus();
					}
					return;
				} else {
					// Didn't click it, jump to the next window.
					title_x += window->title_width_ + 1;
					window = window->next_;
				}
			}
		} else {
			// Clicked the body of the window.
			(void)DockFrame.focused_window_->MouseEvent(
				screen_x, screen_y, button, is_button_down);
		}
	}
}

void Frame::Draw(int min_x, int min_y, int max_x, int max_y) {
	// Skip this frame if it's out of our redraw region.
	if (x_ >= max_x || y_ >= max_y ||
		x_ + width_ < min_x || y_ + height_ < min_y) {
		return;
	}

	if (is_split_frame_) {
		// We are a split fram, with a middle and two children.
		if(SplitFrame.is_split_vertically_) {
			// We are split vertically.
			int y = y_ + SplitFrame.split_point_;
			if (y + 1 >= min_y && y <= max_y) {
				// The middle bar in the draw region.
				DrawSolidColor(std::max(x_, min_x),
					std::max(y, min_y),
					std::min(x_ + width_, max_x),
					std::min(y + 2, max_y),
					SPLIT_BORDER_COLOR);
			}
		} else {
			// We are split horizontally.
			int x = x_ + SplitFrame.split_point_;
			if (x + 1 >= min_x && x <= max_x) {
				// The middle bar in the draw region.
				DrawSolidColor(std::max(x, min_x),
					std::max(y_, min_y),
					std::min(x + 2, max_x),
					std::min(y_ + height_, max_y),
					SPLIT_BORDER_COLOR);
			}
		}

		SplitFrame.child_a_->Draw(min_x, min_y, max_x, max_y);
		SplitFrame.child_b_->Draw(min_x, min_y, max_x, max_y);
	} else {
		// This is a dock frame that contains windows.
		if(min_y < y_ + DockFrame.title_height_) {
			// The title area is within our redraw region.
		/*	// Draw the background behind the title.
			FillRectangle(x_, y_, x_ + width_, y_ + DockFrame.title_height_,
				kBackgroundColor, GetWindowManagerTextureData(),
				GetScreenWidth(), GetScreenHeight());*/

			int y = y_;
			int x = x_ + 1;

			// Draw title's left border.
			DrawYLine(x, y + 1, WINDOW_TITLE_HEIGHT, WINDOW_BORDER_COLOUR,
				GetWindowManagerTextureData(), GetScreenWidth(),
				GetScreenHeight());

			Font* font = GetWindowTitleFont();

			Window *w = DockFrame.first_window_;
			while(w) {
				if(width_ + x_ <= x + w->title_width_ + 1) {
					/* draw previous title row's top and bottom border */
					DrawXLine(x_, y, x - x_, WINDOW_BORDER_COLOUR,
						GetWindowManagerTextureData(), GetScreenWidth(),
						GetScreenHeight());

					CopyTexture(std::max(x_, min_x), std::max(y, min_y),
						std::min(x, max_x),
						std::min(y + WINDOW_TITLE_HEIGHT + 2, max_y),
						GetWindowManagerTextureId(),
						std::max(x_, min_x), std::max(y, min_y));

					/* move to the next line */
					y += WINDOW_TITLE_HEIGHT + 1;
					
					DrawXLine(x_, y, x - x_, WINDOW_BORDER_COLOUR,
						GetWindowManagerTextureData(), GetScreenWidth(),
						GetScreenHeight());

					x = x_ + 1;

					/* draw header's left border */
					DrawYLine(x, y + 1, WINDOW_TITLE_HEIGHT,
						WINDOW_BORDER_COLOUR, GetWindowManagerTextureData(),
						GetScreenWidth(),GetScreenHeight());
				}

				/* draw title's right border */
				DrawYLine(x + w->title_width_, y + 1, WINDOW_TITLE_HEIGHT,
					WINDOW_BORDER_COLOUR, GetWindowManagerTextureData(),
					GetScreenWidth(), GetScreenHeight());

				/* draw title's background */
				Window::DrawHeaderBackground(x, y + 1, w->title_width_,
					w->IsFocused() ? FOCUSED_WINDOW_COLOUR :
					w == DockFrame.focused_window_ ? UNFOCUSED_WINDOW_COLOUR :
					UNSELECTED_WINDOW_COLOUR);

				/* write the title */
				font->DrawString(x + 1, y + 3, w->title_,
					WINDOW_TITLE_TEXT_COLOUR, GetWindowManagerTextureData(),
					GetScreenWidth(), GetScreenHeight());


				/* draw the close button */
				if(w->IsFocused()) {
					font->DrawString(x + w->title_width_ - 9, y + 3, "X",
						WINDOW_CLOSE_BUTTON_COLOUR,
						GetWindowManagerTextureData(), GetScreenWidth(),
						GetScreenHeight());
				}

				x += w->title_width_ + 1;

				w = w->next_;
			}

			/* draw previous title row's top border */
			DrawXLine(x_, y, x - x_, WINDOW_BORDER_COLOUR,
				GetWindowManagerTextureData(), GetScreenWidth(),
				GetScreenHeight());

			CopyTexture(std::max(x_, min_x), std::max(y, min_y),
				std::min(x, max_x),
				std::min(y + WINDOW_TITLE_HEIGHT + 1, max_y),
				GetWindowManagerTextureId(),
				std::max(x_, min_x), std::max(y, min_y));

			/* draw bottom border */
			DrawSolidColor(std::max(x_, min_x),
				std::max(y + WINDOW_TITLE_HEIGHT + 1, min_y),
				std::min(x_ + width_, max_x),
				std::min(y + WINDOW_TITLE_HEIGHT + 2, max_y),
				WINDOW_BORDER_COLOUR);
		}

		// Draw the contents of the focused window.
		DockFrame.focused_window_->DrawWindowContents(
			x_, y_ + DockFrame.title_height_,
			min_x, min_y, max_x, max_y);
	}
}

void Frame::Invalidate() {
	InvalidateScreen(x_, y_, x_ + width_, y_ + height_);
}

int Frame::LongestWindowTitleWidth() {
	if (is_split_frame_)
		return 0;

	int longest_window_title_width = 0;
	Window* window = DockFrame.first_window_;
	while (window != nullptr) {
		longest_window_title_width = std::max(
			longest_window_title_width,
			window->title_width_);
		window = window->next_;
	}
	return longest_window_title_width;
}

bool Frame::IsValidSize(int width, int height) {
	if (is_split_frame_) {
		if (SplitFrame.is_split_vertically_) {
			int new_split_point = (int)((float)height *
				SplitFrame.split_percent_);
			int h1 = new_split_point;
			int h2 = height - new_split_point - SPLIT_BORDER_WIDTH;

			return SplitFrame.child_a_->IsValidSize(width, h1) &&
				SplitFrame.child_a_->IsValidSize(width, h2);
		} else {
			int new_split_point = (int)((float)width *
				SplitFrame.split_percent_);
			int w1 = new_split_point;
			int w2 = width - new_split_point - SPLIT_BORDER_WIDTH;

			return SplitFrame.child_a_->IsValidSize(w1, height) &&
				SplitFrame.child_a_->IsValidSize(w2, height);
		}
	} else {
		if (height <= WINDOW_MINIMUM_HEIGHT)
			return false;

		return LongestWindowTitleWidth() + 2 < width;
	}
}

void InitializeFrames() {
	root_frame = nullptr;
	last_focused_frame = nullptr;
	dragging_frame = nullptr;
}