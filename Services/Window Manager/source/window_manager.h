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

#include "perception/window/base_window.h"
#include "perception/window/window_manager.h"
#include "status.h"

class WindowManager : public ::perception::window::WindowManager::Server {
 public:
  StatusOr<::perception::window::CreateWindowResponse> CreateWindow(
      const ::perception::window::CreateWindowRequest& request,
      ::perception::ProcessId sender) override;

  ::perception::Status CloseWindow(
      const ::perception::window::BaseWindow::Client& window,
      ::perception::ProcessId sender) override;

  ::perception::Status SetWindowTexture(
      const ::perception::window::SetWindowTextureParameters& parameters,
      ::perception::ProcessId sender) override;

  ::perception::Status SetWindowTitle(
      const ::perception::window::SetWindowTitleParameters& parameters,
      ::perception::ProcessId sender) override;

  ::perception::Status SystemButtonPushed() override;

  ::perception::Status InvalidateWindow(
      const ::perception::window::InvalidateWindowParameters& parameters,
      ::perception::ProcessId sender) override;

  StatusOr<::perception::window::Size> GetMaximumWindowSize() override;

  StatusOr<::perception::window::DisplayEnvironment> GetDisplayEnvironment()
      override;
};
