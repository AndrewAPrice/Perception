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

#include "window.h"

#include "perception/ui/point.h"
#include "perception/ui/rectangle.h"
#include "perception/window/window_manager.h"
#include "screen.h"
#include "testing.h"

namespace {

using ::perception::ui::Point;
using ::perception::ui::Rectangle;
using ::perception::window::CreateWindowRequest;

TEST(WindowCreationAndTitle) {
  Window::UnfocusAllWindows();
  InitializeScreen();

  CreateWindowRequest request;
  request.window = ::perception::window::BaseWindow::Client(1, 100);
  request.title = "Test Window";
  request.is_resizable = true;
  request.desired_size.width = 400;
  request.desired_size.height = 300;
  request.add_title_bar = true;

  auto status_or_window = Window::CreateWindow(request);
  EXPECT(true, status_or_window.Ok());

  auto window = *status_or_window;
  window->SetTextureId(1);
  EXPECT("Test Window", window->GetTitle());

  window->SetTitle("New Title");
  EXPECT("New Title", window->GetTitle());

  window->Close();
}

TEST(WindowFocusAndZOrdering) {
  Window::UnfocusAllWindows();
  InitializeScreen();

  CreateWindowRequest request1;
  request1.window = ::perception::window::BaseWindow::Client(1, 101);
  request1.title = "Window 1";
  auto window1 = *Window::CreateWindow(request1);
  window1->SetTextureId(1);

  CreateWindowRequest request2;
  request2.window = ::perception::window::BaseWindow::Client(1, 102);
  request2.title = "Window 2";
  auto window2 = *Window::CreateWindow(request2);
  window2->SetTextureId(2);

  // Focus window 1
  EXPECT(true, window1->IsVisible());
  EXPECT(true, window2->IsVisible());
  window1->Focus();
  EXPECT(true, window1->IsFocused());
  EXPECT(false, window2->IsFocused());

  // Focus window 2
  window2->Focus();
  EXPECT(false, window1->IsFocused());
  EXPECT(true, window2->IsFocused());

  // Unfocus all
  Window::UnfocusAllWindows();
  EXPECT(false, window1->IsFocused());
  EXPECT(false, window2->IsFocused());

  window1->Close();
  window2->Close();
}

TEST(WindowBoundsValidationAndSizing) {
  Window::UnfocusAllWindows();
  InitializeScreen();

  CreateWindowRequest request;
  request.window = ::perception::window::BaseWindow::Client(1, 103);
  request.title = "Sizing Test";
  request.is_resizable = true;
  request.desired_size.width = 300;
  request.desired_size.height = 200;
  auto window = *Window::CreateWindow(request);
  window->SetTextureId(1);

  // Minimum size setting
  ::perception::window::Size min_size;
  min_size.width = 200;
  min_size.height = 150;
  window->SetMinimumSize(min_size);

  Rectangle bounds{.origin = {.x = 0.0f, .y = 0.0f},
                   .size = {.width = 100.0f, .height = 100.0f}};
  window->ValidateWindowBounds(bounds);

  // Should enforce minimum size
  EXPECT(200.0f, bounds.size.width);
  EXPECT(150.0f, bounds.size.height);

  window->Close();
}

TEST(WindowMouseCaptureAndCursor) {
  Window::UnfocusAllWindows();
  InitializeScreen();

  CreateWindowRequest request;
  request.window = ::perception::window::BaseWindow::Client(1, 104);
  request.title = "Cursor Test";
  auto window = *Window::CreateWindow(request);
  window->SetTextureId(1);

  window->SetCursor(::perception::window::Cursor::Poke);
  EXPECT(true, window->GetCursor() == ::perception::window::Cursor::Poke);

  window->SetCaptureMouse(true);
  EXPECT(window.get(), Window::GetCaptiveMouseWindow());

  Window::ExitFullScreenOrMouseCapture();
  EXPECT(nullptr, Window::GetCaptiveMouseWindow());

  window->Close();
}

}  // namespace
