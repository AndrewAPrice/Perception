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

#include "compositor.h"
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

TEST(WindowDefaultScreenFractionSizing) {
  Window::UnfocusAllWindows();
  InitializeScreen();

  auto screen_size = GetScreenSize();
  float expected_default_width = screen_size.width * 0.8f;
  float expected_default_height = screen_size.height * 0.8f;

  CreateWindowRequest full_default_req;
  full_default_req.window = ::perception::window::BaseWindow::Client(1, 112);
  full_default_req.title = "Default Size Window";
  full_default_req.is_resizable = true;
  full_default_req.desired_size.width = 0;
  full_default_req.desired_size.height = 0;
  auto full_default_win = *Window::CreateWindow(full_default_req);
  EXPECT(expected_default_width, full_default_win->GetScreenArea().size.width);
  EXPECT(expected_default_height,
         full_default_win->GetScreenArea().size.height);

  CreateWindowRequest partial_default_req;
  partial_default_req.window = ::perception::window::BaseWindow::Client(1, 113);
  partial_default_req.title = "Partial Default Size Window";
  partial_default_req.is_resizable = true;
  partial_default_req.desired_size.width = 400;
  partial_default_req.desired_size.height = 0;
  auto partial_default_win = *Window::CreateWindow(partial_default_req);
  EXPECT(400.0f, partial_default_win->GetScreenArea().size.width);
  EXPECT(expected_default_height,
         partial_default_win->GetScreenArea().size.height);

  full_default_win->Close();
  partial_default_win->Close();
}

TEST(WindowShadowDoesNotOverlapAtBottomRightCorner) {
  Window::UnfocusAllWindows();
  InitializeScreen();
  InitializeCompositor();

  CreateWindowRequest request;
  request.window = ::perception::window::BaseWindow::Client(1, 114);
  request.title = "Shadow Test Window";
  request.is_resizable = true;
  request.desired_size.width = 200;
  request.desired_size.height = 150;
  request.add_title_bar = true;
  auto window = *Window::CreateWindow(request);
  window->SetTextureId(10);

  InvalidateScreen(window->GetScreenAreaWithFrame());
  DrawScreen();

  const auto& commands = GetLastRunDrawCommands().commands;
  std::vector<Rectangle> shadow_rects;
  for (const auto& cmd : commands) {
    if (cmd.type ==
            ::perception::devices::graphics::Command::Type::FILL_RECTANGLE &&
        cmd.fill_rectangle_parameters &&
        (cmd.fill_rectangle_parameters->color == WINDOW_SHADOW_1 ||
         cmd.fill_rectangle_parameters->color == WINDOW_SHADOW_2)) {
      shadow_rects.push_back(Rectangle{
          .origin = {.x = static_cast<float>(
                         cmd.fill_rectangle_parameters->destination.left),
                     .y = static_cast<float>(
                         cmd.fill_rectangle_parameters->destination.top)},
          .size = {.width = static_cast<float>(
                       cmd.fill_rectangle_parameters->size.width),
                   .height = static_cast<float>(
                       cmd.fill_rectangle_parameters->size.height)}});
    }
  }

  // Both bottom and right shadow strips should be present.
  EXPECT(true, shadow_rects.size() >= 2);

  // No two shadow rectangles may overlap at the bottom-right corner.
  for (size_t i = 0; i < shadow_rects.size(); i++) {
    for (size_t j = i + 1; j < shadow_rects.size(); j++)
      EXPECT(false, shadow_rects[i].Intersects(shadow_rects[j]));
  }

  window->Close();
}

TEST(ContentDragKeepsMouseCapturedUntilAllButtonsReleased) {
  Window::UnfocusAllWindows();
  InitializeScreen();
  InitializeMouse();

  CreateWindowRequest req1;
  req1.window = ::perception::window::BaseWindow::Client(1, 115);
  req1.title = "Drag Window";
  req1.is_resizable = true;
  req1.desired_size.width = 200;
  req1.desired_size.height = 150;
  req1.add_title_bar = true;
  auto win1 = *Window::CreateWindow(req1);
  win1->SetTextureId(11);
  win1->SetCursor(::perception::window::Cursor::Drag);

  Point inside_content = win1->GetScreenArea().origin + Point{100.0f, 80.0f};
  Point outside_window = Point{10.0f, 10.0f};

  SetMousePosition(inside_content);
  EXPECT(true, Window::GetCursorAtPoint(GetMousePosition()) ==
                   ::perception::window::Cursor::Drag);

  // Press left mouse button inside content area.
  ::perception::devices::MouseButtonEvent left_down;
  left_down.button = ::perception::devices::MouseButton::Left;
  left_down.is_pressed_down = true;
  ProcessMouseButtonEvent(left_down);

  EXPECT(true, AreAnyMouseButtonsPressed());
  EXPECT(win1.get(), GetPressedWindow().get());

  // Move mouse outside the window while left button is still held down.
  SetMousePosition(outside_window);
  EXPECT(true, AreAnyMouseButtonsPressed());
  EXPECT(win1.get(), GetPressedWindow().get());
  EXPECT(true, Window::GetCursorAtPoint(GetMousePosition()) ==
                   ::perception::window::Cursor::Drag);

  // Press right mouse button while outside the window.
  ::perception::devices::MouseButtonEvent right_down;
  right_down.button = ::perception::devices::MouseButton::Right;
  right_down.is_pressed_down = true;
  ProcessMouseButtonEvent(right_down);

  EXPECT(true, AreAnyMouseButtonsPressed());
  EXPECT(win1.get(), GetPressedWindow().get());

  // Release left mouse button while right mouse button is still held down.
  ::perception::devices::MouseButtonEvent left_up;
  left_up.button = ::perception::devices::MouseButton::Left;
  left_up.is_pressed_down = false;
  ProcessMouseButtonEvent(left_up);

  EXPECT(true, AreAnyMouseButtonsPressed());
  EXPECT(win1.get(), GetPressedWindow().get());
  EXPECT(true, Window::GetCursorAtPoint(GetMousePosition()) ==
                   ::perception::window::Cursor::Drag);

  // Release right mouse button (all buttons now released).
  ::perception::devices::MouseButtonEvent right_up;
  right_up.button = ::perception::devices::MouseButton::Right;
  right_up.is_pressed_down = false;
  ProcessMouseButtonEvent(right_up);

  EXPECT(false, AreAnyMouseButtonsPressed());
  EXPECT(nullptr, GetPressedWindow().get());
  EXPECT(true, Window::GetCursorAtPoint(GetMousePosition()) ==
                   ::perception::window::Cursor::Pointer);

  win1->Close();
}

TEST(MouseScrollRoutingAndModalBlocking) {
  Window::UnfocusAllWindows();
  InitializeScreen();
  InitializeMouse();

  CreateWindowRequest parent_req;
  parent_req.window = ::perception::window::BaseWindow::Client(1, 116);
  parent_req.title = "Scroll Parent Window";
  parent_req.is_resizable = true;
  parent_req.desired_size.width = 200;
  parent_req.desired_size.height = 150;
  parent_req.add_title_bar = true;
  auto parent = *Window::CreateWindow(parent_req);
  parent->SetTextureId(12);

  Point inside_parent = parent->GetScreenArea().origin + Point{50.0f, 50.0f};
  Point outside_parent = Point{5.0f, 5.0f};
  ::perception::devices::RelativeMousePositionEvent scroll_delta;
  scroll_delta.delta_x = 0.0f;
  scroll_delta.delta_y = 1.0f;

  EXPECT(false, parent->MouseScrollEvent(outside_parent, scroll_delta));
  EXPECT(true, parent->MouseScrollEvent(inside_parent, scroll_delta));

  SetMousePosition(inside_parent);
  ProcessMouseScrollEvent(scroll_delta);

  CreateWindowRequest child_req;
  child_req.window = ::perception::window::BaseWindow::Client(1, 117);
  child_req.parent_window = parent->GetWindowListener();
  child_req.title = "Modal Child";
  child_req.is_resizable = false;
  child_req.desired_size.width = 100;
  child_req.desired_size.height = 80;
  child_req.add_title_bar = true;
  auto child = *Window::CreateWindow(child_req);
  child->SetTextureId(13);

  // Scroll event on a parent with a modal child is consumed without crashing.
  EXPECT(true, parent->MouseScrollEvent(inside_parent, scroll_delta));

  child->Close();
  parent->Close();
}

}  // namespace

