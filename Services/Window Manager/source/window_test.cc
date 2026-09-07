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

TEST(WindowParentChildRelationship) {
  Window::UnfocusAllWindows();
  InitializeScreen();

  CreateWindowRequest parent_req;
  parent_req.window = ::perception::window::BaseWindow::Client(1, 105);
  parent_req.title = "Parent Window";
  parent_req.is_resizable = true;
  parent_req.desired_size.width = 400;
  parent_req.desired_size.height = 300;
  parent_req.add_title_bar = true;
  auto parent = *Window::CreateWindow(parent_req);
  parent->SetTextureId(1);

  EXPECT(true, parent->IsFocused());
  EXPECT(false, parent->HasModalChild());

  CreateWindowRequest child_req;
  child_req.window = ::perception::window::BaseWindow::Client(1, 106);
  child_req.parent_window = parent->GetWindowListener();
  child_req.title = "Child Dialog";
  child_req.is_resizable = false;
  child_req.desired_size.width = 200;
  child_req.desired_size.height = 150;
  child_req.add_title_bar = true;
  auto child = *Window::CreateWindow(child_req);
  child->SetTextureId(2);

  EXPECT(true, parent->HasModalChild());
  EXPECT(false, parent->IsFocused());
  EXPECT(true, child->IsFocused());

  // Attempting to focus parent redirects to child.
  parent->Focus();
  EXPECT(false, parent->IsFocused());
  EXPECT(true, child->IsFocused());

  // Cursor over parent is Pointer (not resize, drag, or button poke).
  auto parent_pos = parent->GetScreenArea().origin + Point{10.0f, 10.0f};
  EXPECT(true, Window::GetCursorAtPoint(parent_pos) ==
                   ::perception::window::Cursor::Pointer);

  // Mouse click on parent redirects focus to child.
  MouseButtonEvent btn_down{.button = ::perception::devices::MouseButton::Left,
                            .is_pressed_down = true};
  bool handled = parent->MouseEvent(parent_pos, btn_down);
  EXPECT(true, handled);
  EXPECT(false, parent->IsFocused());
  EXPECT(true, child->IsFocused());

  // Closing child restores focus to parent.
  child->Close();
  EXPECT(false, parent->HasModalChild());
  EXPECT(true, parent->IsFocused());

  parent->Close();
}

TEST(ChainedModalDialogs) {
  Window::UnfocusAllWindows();
  InitializeScreen();

  CreateWindowRequest req_a;
  req_a.window = ::perception::window::BaseWindow::Client(1, 107);
  req_a.title = "Window A";
  auto win_a = *Window::CreateWindow(req_a);
  win_a->SetTextureId(1);

  CreateWindowRequest req_b;
  req_b.window = ::perception::window::BaseWindow::Client(1, 108);
  req_b.parent_window = win_a->GetWindowListener();
  req_b.title = "Dialog B";
  auto win_b = *Window::CreateWindow(req_b);
  win_b->SetTextureId(2);

  CreateWindowRequest req_c;
  req_c.window = ::perception::window::BaseWindow::Client(1, 109);
  req_c.parent_window = win_b->GetWindowListener();
  req_c.title = "Dialog C";
  auto win_c = *Window::CreateWindow(req_c);
  win_c->SetTextureId(3);

  EXPECT(true, win_c->IsFocused());
  EXPECT(false, win_b->IsFocused());
  EXPECT(false, win_a->IsFocused());

  // Clicking Window A focuses Dialog C.
  win_a->Focus();
  EXPECT(true, win_c->IsFocused());

  // Clicking Dialog B focuses Dialog C.
  win_b->Focus();
  EXPECT(true, win_c->IsFocused());

  // Close Dialog C -> Dialog B is focused.
  win_c->Close();
  EXPECT(true, win_b->IsFocused());
  EXPECT(false, win_a->IsFocused());

  // Clicking Window A focuses Dialog B.
  win_a->Focus();
  EXPECT(true, win_b->IsFocused());

  // Close Dialog B -> Window A is focused.
  win_b->Close();
  EXPECT(true, win_a->IsFocused());

  win_a->Close();
}

TEST(ParentCloseCascadesToChildren) {
  Window::UnfocusAllWindows();
  InitializeScreen();

  CreateWindowRequest req_p;
  req_p.window = ::perception::window::BaseWindow::Client(1, 110);
  req_p.title = "Parent";
  auto win_p = *Window::CreateWindow(req_p);
  win_p->SetTextureId(1);

  CreateWindowRequest req_c;
  req_c.window = ::perception::window::BaseWindow::Client(1, 111);
  req_c.parent_window = win_p->GetWindowListener();
  req_c.title = "Child";
  auto win_c = *Window::CreateWindow(req_c);
  win_c->SetTextureId(2);

  EXPECT(true, win_c->IsVisible());

  // Closing parent closes child.
  win_p->Close();
  EXPECT(false, win_c->IsVisible());
}

}  // namespace
