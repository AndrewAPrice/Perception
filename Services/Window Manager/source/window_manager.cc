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
#include "perception/ui/point.h"
#include "perception/ui/rectangle.h"
#include "screen.h"
#include "status.h"
#include "window.h"

using ::perception::ProcessId;
using ::perception::Status;
using ::perception::ui::Point;
using ::perception::ui::Rectangle;
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
  ASSIGN_OR_RETURN(auto window, Window::CreateWindow(request));

  if (!window) {
    std::cout << "Internal error creating new window " << request.title
              << std::endl;
    return Status::INTERNAL_ERROR;
  }

  auto window_size = window->GetScreenArea().size;

  CreateWindowResponse response;
  response.window_size = {window_size.width, window_size.height};
  return response;
}

Status WindowManager::CloseWindow(const BaseWindow::Client& window,
                                  ::perception::ProcessId /*sender*/) {
  std::cout << "Implement WindowManager::HandleCloseWindow" << std::endl;
  return Status::UNIMPLEMENTED;
}

Status WindowManager::SetWindowTexture(
    const SetWindowTextureParameters& parameters,
    ::perception::ProcessId sender) {
  auto window = GetWindowWithListener(parameters.window);
  if (!window) return Status::INVALID_ARGUMENT;

  window->SetTextureId(parameters.texture.id);
  return Status::OK;
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
  auto window = GetWindowWithListener(parameters.window);
  if (!window) return Status::INVALID_ARGUMENT;

  window->InvalidateLocalArea(
      Rectangle::FromMinMaxPoints(Point{parameters.left, parameters.top},
                                  Point{parameters.right, parameters.bottom}));
  return Status::OK;
}

StatusOr<Size> WindowManager::GetMaximumWindowSize() {
  auto screen_size = GetScreenSize();
  return Size(screen_size.width, screen_size.height);
}

StatusOr<DisplayEnvironment> WindowManager::GetDisplayEnvironment() {
  return Status::UNIMPLEMENTED;
}

Status WindowManager::StartDraggingWindow(
    const BaseWindow::Client& window_listener, ::perception::ProcessId sender) {
  auto window = GetWindowWithListener(window_listener);
  if (!window || sender != window_listener.ServerProcessId())
    return Status::INVALID_ARGUMENT;

  window->StartDragging();
  return Status::OK;
}