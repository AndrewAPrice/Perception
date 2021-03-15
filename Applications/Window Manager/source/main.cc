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

#include "compositor.h"
#include "frame.h"
#include "highlighter.h"
#include "mouse.h"
#include "screen.h"
#include "window.h"
#include "window_manager.h"

#include "perception/scheduler.h"
#include <iostream>

using ::perception::WaitForMessagesThenReturn;


int main() {
	InitializeScreen();
	InitializeMouse();
	InitializeCompositor();
	InitializeHighlighter();
	InitializeFrames();
	InitializeWindows();
	WindowManager window_manager;

	// Draw the entire screen.
	InvalidateScreen(0, 0, GetScreenWidth(), GetScreenHeight());
	DrawScreen();
/*
	for (int i = 0; i < 10; i++) {
	Window::CreateDialog(
			"hello",
			200,
			200,
			0xABCDEFFF);
	
	Window::CreateWindow(
			"goodbye",
			0xFFEEDDFF);


	Window::CreateDialog(
			"parcel",
			400,
			100,
			0xABCDEFFF);
	
	Window::CreateWindow(
			"box",
			0xFFEEDDFF);
	}*/

	while (true) {
		// Sleep until we have messages, then process them.
		WaitForMessagesThenReturn();

		// Redraw the screen once we are done processing all messages.
		DrawScreen();		
	}

	return 0;
}