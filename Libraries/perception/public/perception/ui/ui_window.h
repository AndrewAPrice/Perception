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

#include "perception/ui/widget.h"
#include "permebuf/Libraries/perception/devices/graphics_driver.permebuf.h"
#include "permebuf/Libraries/perception/devices/keyboard_driver.permebuf.h"
#include "permebuf/Libraries/perception/devices/keyboard_listener.permebuf.h"
#include "permebuf/Libraries/perception/devices/mouse_driver.permebuf.h"
#include "permebuf/Libraries/perception/devices/mouse_listener.permebuf.h"
#include "permebuf/Libraries/perception/window.permebuf.h"
#include "permebuf/Libraries/perception/window_manager.permebuf.h"

#include <memory>
#include <string_view>

namespace perception {

class SharedMemory;

namespace ui {

struct DrawContext;

class UiWindow : protected ::permebuf::perception::devices::MouseListener::Server,
				 protected ::permebuf::perception::devices::KeyboardListener::Server,
				 protected ::permebuf::perception::Window::Server,
				 public Widget {
public:
	UiWindow(std::string_view title,
			 bool dialog = false, int dialog_width = 0, int dialog_height = 0);
	virtual ~UiWindow();

	int GetWidth() { return width_; }
	int GetHeight() { return height_; }

	UiWindow* SetRoot(std::shared_ptr<Widget> root);
	UiWindow* SetBackgroundColor(uint32 background_color);
	std::shared_ptr<Widget> GetRoot();
	UiWindow* OnClose(std::function<void()> on_close_handler);

	void Draw();

protected:
	void HandleOnMouseMove(
		ProcessId, const ::permebuf::perception::devices::MouseListener::OnMouseMoveMessage& message) override;
	void HandleOnMouseScroll(
		ProcessId, const ::permebuf::perception::devices::MouseListener::OnMouseScrollMessage& message) override;
	void HandleOnMouseButton(
		ProcessId, const ::permebuf::perception::devices::MouseListener::OnMouseButtonMessage& message) override;
	void HandleOnMouseClick(
		ProcessId, const ::permebuf::perception::devices::MouseListener::OnMouseClickMessage& message) override;
	void HandleOnMouseEnter(
		ProcessId, const ::permebuf::perception::devices::MouseListener::OnMouseEnterMessage& message) override;
	void HandleOnMouseLeave(
		ProcessId, const ::permebuf::perception::devices::MouseListener::OnMouseLeaveMessage& message);
	void HandleOnMouseHover(
		ProcessId, const ::permebuf::perception::devices::MouseListener::OnMouseHoverMessage& message);
	void HandleOnMouseTakenCaptive(
		ProcessId, const ::permebuf::perception::devices::MouseListener::OnMouseTakenCaptiveMessage& message);
	void HandleOnMouseReleased(
		ProcessId, const ::permebuf::perception::devices::MouseListener::OnMouseReleasedMessage& message);
	void HandleOnKeyDown(ProcessId,
		const ::permebuf::perception::devices::KeyboardListener::OnKeyDownMessage& message) override;
	void HandleOnKeyUp(ProcessId,
		const ::permebuf::perception::devices::KeyboardListener::OnKeyUpMessage& message) override;
	void HandleOnKeyboardTakenCaptive(ProcessId,
		const ::permebuf::perception::devices::KeyboardListener::OnKeyboardTakenCaptiveMessage& message) override;
	void HandleOnKeyboardReleased(ProcessId,
		const ::permebuf::perception::devices::KeyboardListener::OnKeyboardReleasedMessage& message) override;
	void HandleSetSize(ProcessId,
		const ::permebuf::perception::Window::SetSizeMessage& message) override;
	void HandleClosed(ProcessId,
		const ::permebuf::perception::Window::ClosedMessage& message) override;
	void HandleGainedFocus(ProcessId,
		const ::permebuf::perception::Window::GainedFocusMessage& message) override;
	void HandleLostFocus(ProcessId,
		const ::permebuf::perception::Window::LostFocusMessage& message) override;

	virtual void Draw(DrawContext& draw_context) override;
    virtual void OnNewHeight(int height) override;
    virtual void OnNewWidth(int width) override;
    virtual void InvalidateChildrensCalculatedWidth() override;
    virtual void InvalidateChildrensCalculatedHeight() override;
    virtual int CalculateContentWidth() override;
    virtual int CalculateContentHeight() override;

    virtual void InvalidateRender() override;

    virtual bool GetWidgetAt(int x, int y,
        std::shared_ptr<Widget>& widget,
        int& x_in_selected_widget,
        int& y_in_selected_widget) override;

private:
	bool invalidated_;

	std::string title_;
	std::shared_ptr<Widget> root_;
	uint32 background_color_;
	std::function<void()> on_close_handler_;

	std::weak_ptr<Widget> widget_mouse_is_over_;

	bool rebuild_texture_;
	int texture_id_;
	int frontbuffer_texture_id_;
	SharedMemory texture_shared_memory_;
	SharedMemory frontbuffer_shared_memory_;

	void SwitchToMouseOverWidget(std::shared_ptr<Widget> widget);
	void ReleaseTextures();
};

}
}
