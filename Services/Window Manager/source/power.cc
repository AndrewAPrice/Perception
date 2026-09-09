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

#include "power.h"

#include <memory>

#include "compositor.h"
#include "perception/devices/keyboard_device.h"
#include "perception/devices/keyboard_listener.h"
#include "perception/devices/power_manager.h"
#include "perception/power.h"
#include "perception/services.h"
#include "screen.h"
#include "window.h"

using ::perception::devices::KeyboardDevice;
using ::perception::devices::KeyboardEvent;
using ::perception::devices::KeyboardListener;
using ::perception::devices::PowerListener;
using ::perception::ui::Rectangle;

namespace {

// Flag indicating whether the system is currently in sleep mode.
bool is_sleeping = false;

class WindowManagerPowerListener : public PowerListener::Server {
 public:
  Status OnSleep() override {
    SetSystemSleeping(true);
    return Status::OK;
  }

  Status OnWake() override {
    SetSystemSleeping(false);
    return Status::OK;
  }
};

class SleepWakeKeyboardListener : public KeyboardListener::Server {
 public:
  Status KeyDown(const KeyboardEvent& event) override {
    if (is_sleeping) ::perception::power::Wake();
    return Status::OK;
  }

  Status KeyUp(const KeyboardEvent& event) override {
    if (is_sleeping) ::perception::power::Wake();
    return Status::OK;
  }

  Status KeyboardTakenCaptive() override { return Status::OK; }
  Status KeyboardReleased() override { return Status::OK; }
};

std::unique_ptr<WindowManagerPowerListener> power_listener;
std::unique_ptr<SleepWakeKeyboardListener> sleep_keyboard_listener;

}  // namespace

bool IsSystemSleeping() { return is_sleeping; }

void SetSystemSleeping(bool sleeping) {
  if (is_sleeping == sleeping) return;
  is_sleeping = sleeping;

  if (is_sleeping) {
    if (!sleep_keyboard_listener)
      sleep_keyboard_listener = std::make_unique<SleepWakeKeyboardListener>();
    ::perception::GetService<KeyboardDevice>().SetKeyboardListener(
        *sleep_keyboard_listener, nullptr);

    InvalidateScreen(Rectangle{.size = GetScreenSize()});
    DrawScreen();
  } else {
    Window::UpdateKeyboardListener();

    InvalidateScreen(Rectangle{.size = GetScreenSize()});
    DrawScreen();
  }
}

void InitializePower() {
  power_listener = std::make_unique<WindowManagerPowerListener>();
}
