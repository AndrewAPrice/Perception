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
#include "status.h"
#include "window.h"

using ::perception::ProcessId;
using ::perception::Status;
using ::perception::window::BaseWindow;
using ::perception::window::CreateWindowRequest;
using ::perception::window::CreateWindowResponse;
using ::perception::window::DisplayEnvironment;
using ::perception::window::InvalidateWindowParameters;
using ::perception::window::SetWindowTextureParameters;
using ::perception::window::SetWindowTitleParameters;
using ::perception::window::Size;

StatusOr<CreateWindowResponse> WindowManager::CreateWindow(
    const CreateWindowRequest& request, ProcessId sender) {
  Window* window;

  if (request.is_dialog) {
    window = Window::CreateDialog(
        request.title, request.desired_dialog_size.width,
        request.desired_dialog_size.height, request.fill_color, request.window,
        request.keyboard_listener, request.mouse_listener);
  } else {
    window =
        Window::CreateWindow(request.title, request.fill_color, request.window,
                             request.keyboard_listener, request.mouse_listener);
  }

  if (window == nullptr) return Status::INTERNAL_ERROR;

  CreateWindowResponse response;
  response.window_size = {window->GetWidth(), window->GetHeight()};
  return response;
}

Status WindowManager::CloseWindow(const BaseWindow::Client& window,
                                  ::perception::ProcessId sender) {
  std::cout << "Implement WindowManager::HandleCloseWindow" << std::endl;
  return Status::UNIMPLEMENTED;
}

Status WindowManager::SetWindowTexture(
    const SetWindowTextureParameters& parameters,
    ::perception::ProcessId sender) {
  Window* window = Window::GetWindow(parameters.window);
  if (window == nullptr) return Status::INVALID_ARGUMENT;

  window->SetTextureId(parameters.texture.id);
  return Status::PROCESS_DOESNT_EXIST;
}

Status WindowManager::SetWindowTitle(const SetWindowTitleParameters& parameters,
                                     ::perception::ProcessId sender) {
  std::cout << "Implement WindowManager::HandleWindowTitle" << std::endl;
  return Status::UNIMPLEMENTED;
}

Status WindowManager::SystemButtonPushed() {
  // TODO: show launcher
  // ShowLauncher();

  return Status::OK;
}

Status WindowManager::InvalidateWindow(
    const InvalidateWindowParameters& parameters,
    ::perception::ProcessId sender) {
  Window* window = Window::GetWindow(parameters.window);
  if (window == nullptr) return Status::INVALID_ARGUMENT;

  window->InvalidateContents(parameters.left, parameters.top, parameters.right,
                             parameters.bottom);
  return Status::OK;
}

StatusOr<Size> WindowManager::GetMaximumWindowSize() {
  return Size(GetScreenWidth(), GetScreenHeight() - WINDOW_TITLE_HEIGHT - 3);
}

StatusOr<DisplayEnvironment> WindowManager::GetDisplayEnvironment() {
  return Status::UNIMPLEMENTED;
}