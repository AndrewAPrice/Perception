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

#include "window_manager.h"

#include <iostream>

#include "perception/launcher.h"
#include "window.h"

using ::perception::ShowLauncher;
using WM = ::permebuf::perception::WindowManager;

StatusOr<WM::CreateWindowResponse> WindowManager::HandleCreateWindow(
	::perception::ProcessId sender,
	Permebuf<WindowManager::WM::CreateWindowRequest> request) {
	Window* window;

	if (request->GetIsDialog()) {
		window = Window::CreateDialog(
			*request->GetTitle(),
			request->GetDesiredDialogWidth(),
			request->GetDesiredDialogHeight(),
			request->GetFillColor(),
			request->GetWindow(),
			request->GetKeyboardListener(),
			request->GetMouseListener());
	} else {
		window = Window::CreateWindow(
			*request->GetTitle(),
			request->GetFillColor(),
			request->GetWindow(),
			request->GetKeyboardListener(),
			request->GetMouseListener());
	}

	WindowManager::WM::CreateWindowResponse response;
	if (window != nullptr) {
		// Respond with the window dimensions if we were able to create this
		// window.
		response.SetWidth(window->GetWidth());
		response.SetHeight(window->GetHeight());
	}
	return response;
}

void WindowManager::HandleCloseWindow(
	::perception::ProcessId sender,
	const WindowManager::WM::CloseWindowMessage& message) {
	std::cout << "Implement WindowManager::HandleCloseWindow" << std::endl;
}

void WindowManager::HandleSetWindowTexture(
	::perception::ProcessId sender,
	const WindowManager::WM::SetWindowTextureMessage& message) {
	Window* window = Window::GetWindow(message.GetWindow());
	if (window != nullptr)
		window->SetTextureId(message.GetTextureId());
}

void WindowManager::HandleSetWindowTitle(
	::perception::ProcessId sender,
	Permebuf<WindowManager::WM::SetWindowTitleMessage> message) {
	std::cout << "Implement WindowManager::HandleWindowTitle" << std::endl;
}

void WindowManager::HandleSystemButtonPushed(
	::perception::ProcessId sender,
	const WindowManager::WM::SystemButtonPushedMessage& message) {
	ShowLauncher();
}

void WindowManager::HandleInvalidateWindow(
	::perception::ProcessId sender,
	const WindowManager::WM::InvalidateWindowMessage& message) {
	Window* window = Window::GetWindow(message.GetWindow());
	if (window != nullptr)
		window->InvalidateContents(message.GetLeft(),
			message.GetTop(), message.GetRight(),
			message.GetBottom());

}