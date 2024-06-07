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

#include "compositor.h"
#include "perception/launcher.h"
#include "screen.h"
#include "window.h"

using ::perception::ShowLauncher;
using WM = ::permebuf::perception::WindowManager;

StatusOr<Permebuf<WM::CreateWindowResponse>> WindowManager::HandleCreateWindow(
    ::perception::ProcessId sender,
    Permebuf<WindowManager::WM::CreateWindowRequest> request) {
  Window* window;

  if (request->GetIsDialog()) {
    window = Window::CreateDialog(
        *request->GetTitle(), request->GetDesiredDialogWidth(),
        request->GetDesiredDialogHeight(), request->GetFillColor(),
        request->GetWindow(), request->GetKeyboardListener(),
        request->GetMouseListener());
  } else {
    window = Window::CreateWindow(
        *request->GetTitle(), request->GetFillColor(), request->GetWindow(),
        request->GetKeyboardListener(), request->GetMouseListener());
  }

  Permebuf<WM::CreateWindowResponse> response;
  if (window != nullptr) {
    // Respond with the window dimensions if we were able to create this
    // window.
    response->SetWidth(window->GetWidth());
    response->SetHeight(window->GetHeight());
  }
  return response;
}

void WindowManager::HandleCloseWindow(::perception::ProcessId sender,
                                      const WM::CloseWindowMessage& message) {
  std::cout << "Implement WindowManager::HandleCloseWindow" << std::endl;
}

void WindowManager::HandleSetWindowTexture(
    ::perception::ProcessId sender,
    const WM::SetWindowTextureMessage& message) {
  Window* window = Window::GetWindow(message.GetWindow());
  if (window != nullptr) window->SetTextureId(message.GetTextureId());
}

void WindowManager::HandleSetWindowTitle(
    ::perception::ProcessId sender,
    Permebuf<WM::SetWindowTitleMessage> message) {
  std::cout << "Implement WindowManager::HandleWindowTitle" << std::endl;
}

void WindowManager::HandleSystemButtonPushed(
    ::perception::ProcessId sender,
    const WM::SystemButtonPushedMessage& message) {
  ShowLauncher();
}

void WindowManager::HandleInvalidateWindow(
    ::perception::ProcessId sender,
    const WM::InvalidateWindowMessage& message) {
  Window* window = Window::GetWindow(message.GetWindow());
  if (window != nullptr)
    window->InvalidateContents(message.GetLeft(), message.GetTop(),
                               message.GetRight(), message.GetBottom());
}

StatusOr<WM::GetMaximumWindowSizeResponse>
WindowManager::HandleGetMaximumWindowSize(
    ::perception::ProcessId sender,
    const WM::GetMaximumWindowSizeRequest& message) {
  WM::GetMaximumWindowSizeResponse response;
  response.SetWidth(GetScreenWidth());
  response.SetHeight(GetScreenHeight() - WINDOW_TITLE_HEIGHT - 3);
  return response;
}