// Copyright 2026 Google LLC
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

#include "perception/ui/size.h"
#include "perception/window/window_manager.h"
#include "screen.h"
#include "status.h"
#include "testing.h"
#include "window.h"

namespace {

using ::perception::window::CreateWindowRequest;
using ::perception::window::SetWindowTitleParameters;

TEST(WindowManagerScaleAndDisplayQueries) {
  Window::UnfocusAllWindows();
  InitializeScreen();

  WindowManager service;

  // Test scale query
  EXPECT(1.0f, WindowManager::GetScale());

  // Test maximum window size query
  auto status_or_max_size = service.GetMaximumWindowSize();
  EXPECT(true, status_or_max_size.Ok());
  auto max_size = *status_or_max_size;
  EXPECT(1920.0f, max_size.width);
  EXPECT(1080.0f, max_size.height);

  // Test display bounds query
  auto status_or_bounds = service.GetDisplayBounds();
  EXPECT(true, status_or_bounds.Ok());
  EXPECT(1, static_cast<int>((*status_or_bounds).bounds.size()));
}

TEST(WindowManagerServiceRPCMethods) {
  Window::UnfocusAllWindows();
  InitializeScreen();

  WindowManager service;

  CreateWindowRequest create_req;
  create_req.window = ::perception::window::BaseWindow::Client(1, 200);
  create_req.title = "RPC Test Window";
  create_req.is_resizable = true;

  auto status_or_res = service.CreateWindow(create_req, /*sender=*/1);
  EXPECT(true, status_or_res.Ok());

  // Update window title via RPC (sender matching window process ID = 1)
  SetWindowTitleParameters title_params;
  title_params.window = create_req.window;
  title_params.title = "Updated RPC Title";
  Status title_status = service.SetWindowTitle(title_params, /*sender=*/1);
  EXPECT(true, title_status == Status::OK);

  // Close window via RPC (sender matching window process ID = 1)
  Status close_status = service.CloseWindow(create_req.window, /*sender=*/1);
  EXPECT(true, close_status == Status::OK);
}

}  // namespace
