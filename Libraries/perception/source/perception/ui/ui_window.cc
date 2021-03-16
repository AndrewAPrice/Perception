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

#include "perception/draw.h"
#include "perception/scheduler.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/theme.h"
#include "perception/ui/ui_window.h"

using ::permebuf::perception::devices::GraphicsCommand;
using ::permebuf::perception::devices::GraphicsDriver;
using ::permebuf::perception::devices::KeyboardDriver;
using ::permebuf::perception::devices::KeyboardListener;
using ::permebuf::perception::devices::MouseButton;
using ::permebuf::perception::devices::MouseDriver;
using ::permebuf::perception::devices::MouseListener;
using ::permebuf::perception::Window;
using ::permebuf::perception::WindowManager;

namespace perception {
namespace ui {

UiWindow::UiWindow(std::string_view title,
		bool dialog, int dialog_width, int dialog_height) :
	title_(title), background_color_(kBackgroundWindowColor), texture_id_(0),
	frontbuffer_texture_id_(0), rebuild_texture_(true) {
	Permebuf<WindowManager::CreateWindowRequest> create_window_request;
	//std::cout << title_ << "'s message id is: " << 
	//((Window::Server*)this)->GetProcessId() << ":" <<
	//((Window::Server*)this)->GetMessageId() << std::endl;
	create_window_request->SetWindow(*this);
	create_window_request->SetTitle(title);
	create_window_request->SetFillColor(0xFFFFFFFF);
	create_window_request->SetKeyboardListener(*this);
	create_window_request->SetMouseListener(*this);
	if (dialog) {
		create_window_request->SetIsDialog(true);
		create_window_request->SetDesiredDialogWidth(dialog_width);
		create_window_request->SetDesiredDialogHeight(dialog_height);
	}

	auto status_or_result = WindowManager::Get().CallCreateWindow(
		std::move(create_window_request));
	if (status_or_result) {
		width_ = status_or_result->GetWidth();
		height_ = status_or_result->GetHeight();
	} else {
		width_ = 0;
		height_ = 0;
	}
}

UiWindow::~UiWindow() {
	// std::cout << title_ << " - destroyed" << std::endl;
}

UiWindow* UiWindow::SetRoot(std::shared_ptr<Widget> root) {
	if (root_ == root)
		return this;

	if (root_)
		root_->ClearParent();

	root_ = root;
	root_->SetParent(ToSharedPtr());
	InvalidateRender();
	return this;
}

UiWindow* UiWindow::SetBackgroundColor(uint32 background_color) {
	if (background_color_ == background_color)
		return this;

	background_color_ = background_color;
	InvalidateRender();
	return this;
}

std::shared_ptr<Widget> UiWindow::GetRoot() {
	return root_;
}

UiWindow* UiWindow::OnClose(std::function<void()> on_close_handler) {
	on_close_handler_ = on_close_handler;
	return this;
}

void UiWindow::HandleOnMouseMove(
	ProcessId, const MouseListener::OnMouseMoveMessage& message) {
	//	std::cout << title_ << " - x:" << message.GetDeltaX() <<
	//		" y:" << message.GetDeltaY() << std::endl;
}

void UiWindow::HandleOnMouseScroll(
	ProcessId, const MouseListener::OnMouseScrollMessage& message) {
	// std::cout << title_ << " mouse scrolled" << message.GetDelta() << std::endl;
}

void UiWindow::HandleOnMouseButton(
	ProcessId, const MouseListener::OnMouseButtonMessage& message) {

	// std::cout << title_ << " - button: " << (int)message.GetButton() <<
	//	" down: " << message.GetIsPressedDown() << std::endl;
}

void UiWindow::HandleOnMouseClick(
	ProcessId, const MouseListener::OnMouseClickMessage& message) {
	std::shared_ptr<Widget> widget;
	int x_in_selected_widget, y_in_selected_widget;
	
	(bool)GetWidgetAt(message.GetX(), message.GetY(),
	    widget, x_in_selected_widget, y_in_selected_widget);

	SwitchToMouseOverWidget(widget);

	// Tell the widget the mouse has moved.
	if (widget) {
		if (message.GetWasPressedDown()) {
			widget->OnMouseButtonDown(x_in_selected_widget, y_in_selected_widget,
				message.GetButton());
		} else {
			widget->OnMouseButtonUp(x_in_selected_widget, y_in_selected_widget,
				message.GetButton());
		}
	}

	// std::cout << title_ << " - mouse clicked - button: " << (int)message.GetButton() <<
	//	" down: " << message.GetWasPressedDown() << " x: " << message.GetX() <<
	//	" y: " << message.GetY() << std::endl;
}

void UiWindow::HandleOnMouseEnter(
	ProcessId, const MouseListener::OnMouseEnterMessage& message) {
	// std::cout << title_ << " - mouse entered." << std::endl;
}

void UiWindow::HandleOnMouseLeave(
	ProcessId, const MouseListener::OnMouseLeaveMessage& message) {
	if (auto widget = widget_mouse_is_over_.lock()) {
		widget->OnMouseLeave();
	}
	widget_mouse_is_over_.reset();
}

void UiWindow::HandleOnMouseHover(
	ProcessId, const MouseListener::OnMouseHoverMessage& message) {
	std::shared_ptr<Widget> widget;
	int x_in_selected_widget, y_in_selected_widget;
	
	(bool)GetWidgetAt(message.GetX(), message.GetY(),
	    widget, x_in_selected_widget, y_in_selected_widget);

	SwitchToMouseOverWidget(widget);

	// Tell the widget the mouse has moved.
	if (widget) {
		widget->OnMouseMove(x_in_selected_widget, y_in_selected_widget);
	}
}

void UiWindow::HandleOnMouseTakenCaptive(
	ProcessId, const MouseListener::OnMouseTakenCaptiveMessage& message) {
	// std::cout << title_ << " - mouse taken captive." << std::endl;
}

void UiWindow::HandleOnMouseReleased(
	ProcessId, const MouseListener::OnMouseReleasedMessage& message) {
	// std::cout << title_ << " - mouse has been released." << std::endl;
}

void UiWindow::HandleOnKeyDown(ProcessId,
	const KeyboardListener::OnKeyDownMessage& message) {
	// std::cout << title_ << " - key " << (int)message.GetKey() << " was pressed." << std::endl;
}

void UiWindow::HandleOnKeyUp(ProcessId,
	const KeyboardListener::OnKeyUpMessage& message) {
	// std::cout << title_ << " - key " << (int)message.GetKey() << " was released." << std::endl;
}

void UiWindow::HandleOnKeyboardTakenCaptive(ProcessId,
	const KeyboardListener::OnKeyboardTakenCaptiveMessage& message) {
	// std::cout << title_ << " - keyboard taken captive." << std::endl;
}

void UiWindow::HandleOnKeyboardReleased(ProcessId,
	const KeyboardListener::OnKeyboardReleasedMessage& message) {
	// std::cout << title_ << " - keyboard has been released." << std::endl;
}

void UiWindow::HandleSetSize(ProcessId,
	const Window::SetSizeMessage& message) {
	std::cout << "Window " << title_ << " resized to " << message.GetWidth() << "," << message.GetHeight() << std::endl;
	SetWidth(message.GetWidth());
	SetHeight(message.GetHeight());
	rebuild_texture_ = true;
	InvalidateRender();
}

void UiWindow::HandleClosed(ProcessId,
	const Window::ClosedMessage& message) {
	if (on_close_handler_)
		on_close_handler_();
}

void UiWindow::HandleGainedFocus(ProcessId,
	const Window::GainedFocusMessage& message) {
	// std::cout << title_ << " gained focus" << std::endl;
}

void UiWindow::HandleLostFocus(ProcessId,
	const Window::LostFocusMessage& message) {
	// std::cout << title_ << " lost focus" << std::endl;
}

void UiWindow::Draw() {
	if (!invalidated_)
		return;

	if (rebuild_texture_) {
		// The window size has changed.
		VerifyCalculatedSize();

		ReleaseTextures();

		if (width_ > 0 && height_ > 0) {
			// Create the back buffer we draw into.
			GraphicsDriver::CreateTextureRequest request;
			request.SetWidth(width_);
			request.SetHeight(height_);
			auto status_or_response =
				GraphicsDriver::Get().CallCreateTexture(request);
			if (status_or_response) {
				texture_id_ = status_or_response->GetTexture();
				texture_shared_memory_ = status_or_response->GetPixelBuffer();
			}

			// Create the front buffer.
			status_or_response =
				GraphicsDriver::Get().CallCreateTexture(request);
			if (status_or_response) {
				frontbuffer_texture_id_ = status_or_response->GetTexture();
				frontbuffer_shared_memory_ = status_or_response->GetPixelBuffer();
			}

			// Notify the window manager of our front buffer.
			WindowManager::SetWindowTextureMessage message;
			message.SetWindow(*this);
			message.SetTextureId(frontbuffer_texture_id_);
			WindowManager::Get().SendSetWindowTexture(message);
		}
		rebuild_texture_ = false;
	}

	if (width_ == 0 || height_ == 0 ||
		!texture_shared_memory_.Join() || !frontbuffer_shared_memory_.Join())
		return;

	// Set up our DrawContext to draw into back buffer.
	DrawContext draw_context;
	draw_context.x = 0;
	draw_context.y = 0;
	draw_context.buffer = static_cast<uint32*>(*texture_shared_memory_);
	draw_context.buffer_width = width_;
	draw_context.buffer_height = height_;

	if (background_color_) {
		FillRectangle(0, 0, width_, height_,
			background_color_, draw_context.buffer,
			draw_context.buffer_width, draw_context.buffer_height);
	}

    if (root_)
    	root_->Draw(draw_context);

    // Copy the backbuffer to the front buffer.
    memcpy(*frontbuffer_shared_memory_, *texture_shared_memory_,
    	width_ * height_ * 4);

    // Tell the window manager the back buffer is ready to draw.
    WindowManager::InvalidateWindowMessage message;
    message.SetWindow(*this);
    message.SetLeft(0);
    message.SetTop(0);
    message.SetRight(static_cast<uint16>(width_));
    message.SetBottom(static_cast<uint16>(height_));
    WindowManager::Get().SendInvalidateWindow(message);

	invalidated_ = false;
}


void UiWindow::OnNewHeight(int height) {
    SetHeight(height);
    if (root_ && root_->GetHeight() == kFillParent) {
        root_->SetCalculatedHeight(height);
    }
    InvalidateRender();
}

void UiWindow::OnNewWidth(int width) {
    SetWidth(width);
    if (root_ && root_->GetWidth() == kFillParent) {
        root_->SetCalculatedWidth(width);
    }
    InvalidateRender();
}

void UiWindow::InvalidateChildrensCalculatedWidth() {
    if (root_ && root_->GetWidth() == kFillParent) {
        root_->InvalidateCalculatedWidth();
    }
}

void UiWindow::InvalidateChildrensCalculatedHeight() {
    if (root_ && root_->GetHeight() == kFillParent) {
        root_->InvalidateCalculatedHeight();
    }
}

int UiWindow::CalculateContentWidth() {
    if (root_ && root_->GetWidth() != kFillParent) {
        return root_->GetCalculatedWidth();
    } else {
        return 0;
    }
}

int UiWindow::CalculateContentHeight() {
    if (root_ && root_->GetHeight() != kFillParent) {
        return root_->GetCalculatedHeight();
    } else {
        return 0;
    }
}

void UiWindow::InvalidateRender() {
    if (invalidated_) {
        return;
    }

    Defer([this]() {
    	Draw();
    });

    invalidated_ = true;
}

bool UiWindow::GetWidgetAt(int x, int y,
    std::shared_ptr<Widget>& widget,
    int& x_in_selected_widget,
    int& y_in_selected_widget) {
	if (root_) {
		return root_->GetWidgetAt(x, y, widget,
			x_in_selected_widget, y_in_selected_widget);
	} else {
		return false;
	}
}

void UiWindow::Draw(DrawContext& draw_context) {}


void UiWindow::SwitchToMouseOverWidget(std::shared_ptr<Widget> widget) {
	auto old_widget = widget_mouse_is_over_.lock();
	if (widget == old_widget)
		return;

	// The widget we are over has changed.
	if (old_widget) 
		old_widget->OnMouseLeave();

	if (widget) {
		// We have a new widget.
		widget->OnMouseEnter();
		widget_mouse_is_over_ = widget;
	} else {
		// There is no new widget.
		widget_mouse_is_over_.reset();
	}
}

void UiWindow::ReleaseTextures() {
	if (texture_id_ != 0) {
		// We have an old texture to release.
		GraphicsDriver::DestroyTextureMessage message;
		message.SetTexture(texture_id_);
		GraphicsDriver::Get().SendDestroyTexture(message);
		texture_id_ = 0;
		texture_shared_memory_ = SharedMemory();
	}

	if (frontbuffer_texture_id_ != 0) {
		// We have an old frontbuffer texture to release.
		GraphicsDriver::DestroyTextureMessage message;
		message.SetTexture(frontbuffer_texture_id_);
		GraphicsDriver::Get().SendDestroyTexture(message);
		frontbuffer_texture_id_ = 0;
		frontbuffer_shared_memory_= SharedMemory();
	}
}

}
}
