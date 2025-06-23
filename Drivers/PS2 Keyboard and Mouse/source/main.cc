// Copyright 2020 Google LLC
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

#include "perception/devices/keyboard_device.h"
#include "perception/devices/keyboard_listener.h"
#include "perception/devices/mouse_device.h"
#include "perception/devices/mouse_listener.h"
#include "perception/interrupts.h"
#include "perception/messages.h"
#include "perception/port_io.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/window/window_manager.h"
#include "status.h"

using ::perception::FindFirstInstanceOfService;
using ::perception::IsDuplicateInstanceOfProcess;
using ::perception::ProcessId;
using ::perception::Read8BitsFromPort;
using ::perception::RegisterInterruptHandler;
using ::perception::Status;
using ::perception::Write8BitsToPort;
using ::perception::devices::KeyboardDevice;
using ::perception::devices::KeyboardListener;
using ::perception::devices::MouseButton;
using ::perception::devices::MouseButtonEvent;
using ::perception::devices::MouseClickEvent;
using ::perception::devices::MouseDevice;
using ::perception::devices::MouseListener;
using ::perception::devices::MousePositionEvent;
using ::perception::devices::RelativeMousePositionEvent;
using ::perception::devices::KeyboardEvent;
using ::perception::window::WindowManager;

namespace {

constexpr size_t kTimeout = 100000;

// The system key (set to Escape) to send to the window manager.
constexpr uint8 kSystemKeyDown = 1;
constexpr uint8 kSystemKeyUp = 129;

class PS2MouseDevice : public MouseDevice::Server {
 public:
  PS2MouseDevice()
      : mouse_bytes_received_(0), last_button_state_{false, false, false} {}

  virtual ~PS2MouseDevice() {
    if (mouse_captor_) {
      // Tell the captor we had to let the mouse go.
      mouse_captor_->MouseReleased();
    }
  }

  void HandleMouseInterrupt() {
    uint8 val = Read8BitsFromPort(0x60);
    if (mouse_bytes_received_ == 2) {
      // Process the message now that we have all 3 bytes.
      ProcessMouseMessage(mouse_byte_buffer_[0], mouse_byte_buffer_[1], val);

      // Reset the cycle.
      mouse_bytes_received_ = 0;
    } else {
      // Read one of the first 2 bytes.
      mouse_byte_buffer_[mouse_bytes_received_] = val;
      mouse_bytes_received_++;
    }
  }

  virtual Status SetMouseListener(
      const MouseListener::Client& listener) override {
    if (mouse_captor_) {
      // Let the old captor know the mouse has escaped.
      mouse_captor_->MouseReleased();
    }
    if (listener.IsValid()) {
      mouse_captor_ = std::make_unique<MouseListener::Client>(listener);
      // Let our captor know they have taken the mouse captive.
      mouse_captor_->MouseTakenCaptive();
    } else {
      mouse_captor_.reset();
    }
    return Status::OK;
  }

 private:
  // Messages from the mouse come in 3 bytes. We need to buffer these until we
  // have enough bytes to process a message.
  uint8 mouse_bytes_received_;
  uint8 mouse_byte_buffer_[2];

  // The last known state of the mouse buttons.
  bool last_button_state_[3];

  // The service we should sent mouse events to.
  std::unique_ptr<MouseListener::Client> mouse_captor_;

  // Processes the mouse message.
  void ProcessMouseMessage(uint8 status, uint8 offset_x, uint8 offset_y) {
    // Read if the mouse has moved.
    int16 delta_x = (int16)offset_x - (((int16)status << 4) & 0x100);
    int16 delta_y = -(int16)offset_y + (((int16)status << 3) & 0x100);

    // Read the left, middle, right buttons.
    bool buttons[3] = {(status & (1)) == 1, (status & (1 << 2)) == 4,
                       (status & (1 << 1)) == 2};

    if ((delta_x != 0 || delta_y != 0) && mouse_captor_) {
      // Send our captor a message that the mouse has moved.
      RelativeMousePositionEvent message;
      message.delta_x = static_cast<float>(delta_x);
      message.delta_y = static_cast<float>(delta_y);
      mouse_captor_->MouseMove(message);
    }

    for (int button_index : {0, 1, 2}) {
      if (buttons[button_index] != last_button_state_[button_index]) {
        last_button_state_[button_index] = buttons[button_index];
        if (mouse_captor_) {
          // Send our captor a message that a mouse button has changed state.
          MouseButtonEvent message;
          switch (button_index) {
            case 0:
              message.button = MouseButton::Left;
              break;
            case 1:
              message.button = MouseButton::Middle;
              break;
            case 2:
              message.button = MouseButton::Right;
              break;
          }
          message.is_pressed_down = buttons[button_index];
          mouse_captor_->MouseButton(message);
        }
      }
    }
  }
};

class PS2KeyboardDevice : public KeyboardDevice::Server {
 public:
  PS2KeyboardDevice() {}

  virtual ~PS2KeyboardDevice() {
    if (keyboard_captor_) {
      // Tell the captor we had to let the keyboard go.
      keyboard_captor_->KeyboardReleased();
    }
  }

  void HandleKeyboardInterrupt() {
    uint8 val = Read8BitsFromPort(0x60);
    if (val == kSystemKeyDown) {
      // The system key was pressed. Notify the window manager.
      auto window_manager = FindFirstInstanceOfService<WindowManager>();
      if (window_manager) window_manager->SystemButtonPushed({});
      return;
    } else if (val == kSystemKeyUp) {
      // Ignore releasing the system key.
      return;
    }

    if (!keyboard_captor_)
      // No one to send the keyboard event to.
      return;

    uint8 key = val & 127;
    if ((val & 128) == 0) {
      // Send our captor a message that the key was pressed down.
      KeyboardEvent message;
      message.key = key;
      keyboard_captor_->KeyDown(message);
    } else {
      // Send our captor a message that the key was released.
      KeyboardEvent message;
      message.key = key;
      keyboard_captor_->KeyUp(message);
    }
  }

  virtual Status SetKeyboardListener(
      const KeyboardListener::Client& listener) override {
    if (keyboard_captor_) {
      // Let the old captor know the keyboard has escaped.
      keyboard_captor_->KeyboardReleased();
    }
    if (listener.IsValid()) {
      keyboard_captor_ = std::make_unique<KeyboardListener::Client>(listener);
      // Let our captor know they have taken the keybord captive.
      keyboard_captor_->KeyboardTakenCaptive();
    } else {
      keyboard_captor_.reset();
    }
    return Status::OK;
  }

 private:
  // The service we should sent keyboard events to.
  std::unique_ptr<KeyboardListener> keyboard_captor_;
};

// Global instance of the mouse device.
std::unique_ptr<PS2MouseDevice> mouse_device;

// Global instance of the keyboard device.
std::unique_ptr<PS2KeyboardDevice> keyboard_device;

void InterruptHandler() {
  uint8 check;

  // Keep looping while there are bytes (the mouse will send multiple bytes.)
  while ((check = Read8BitsFromPort(0x64)) & 1) {
    if (check & (1 << 5)) {
      mouse_device->HandleMouseInterrupt();
    } else {
      keyboard_device->HandleKeyboardInterrupt();
    }
  }
}

void WaitForMouseData() {
  size_t timeout = kTimeout;
  while (timeout--) {
    if ((Read8BitsFromPort(0x64) & 1) == 1) {
      return;
    }
  }
}

void WaitForMouseSignal() {
  size_t timeout = kTimeout;
  while (timeout--) {
    if ((Read8BitsFromPort(0x64) & 2) == 0) return;
  }
}

void MouseWrite(uint8 b) {
  WaitForMouseSignal();
  Write8BitsToPort(0x64, 0xD4);
  WaitForMouseSignal();
  Write8BitsToPort(0x60, b);
}

uint8 MouseRead() {
  WaitForMouseData();
  return Read8BitsFromPort(0x60);
}

void InitializePS2Controller() {
  // Enable auxiliary device.
  WaitForMouseSignal();
  Write8BitsToPort(0x64, 0xA8);

  // Enable the interrupts.
  WaitForMouseSignal();
  Write8BitsToPort(0x64, 0x20);
  WaitForMouseData();
  uint8 status = Read8BitsFromPort(0x60) | 2;
  WaitForMouseSignal();

  Write8BitsToPort(0x64, 0x60);
  WaitForMouseSignal();
  Write8BitsToPort(0x60, status);

  // Set the default values.
  MouseWrite(0xF6);
  (void)MouseRead();

  // Enable packet streaming.
  MouseWrite(0xF4);
  (void)MouseRead();
}

}  // namespace

int main(int argc, char* argv[]) {
  if (IsDuplicateInstanceOfProcess()) return 0;

  mouse_device = std::make_unique<PS2MouseDevice>();
  keyboard_device = std::make_unique<PS2KeyboardDevice>();
  InitializePS2Controller();

  // Listen to the interrupts.
  RegisterInterruptHandler(1, InterruptHandler);
  RegisterInterruptHandler(12, InterruptHandler);

  perception::HandOverControl();
  return 0;
}
