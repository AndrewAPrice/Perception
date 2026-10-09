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
#include <iostream>

#include "perception/devices/keyboard_device.h"
#include "perception/devices/keyboard_listener.h"
#include "perception/devices/mouse_device.h"
#include "perception/devices/mouse_listener.h"
#include "perception/interrupts.h"
#include "perception/messages.h"
#include "perception/port_io.h"
#include "perception/processes.h"
#include "perception/profiling.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/window/window_manager.h"
#include "status.h"

using ::perception::FindFirstInstanceOfService;
using ::perception::IsDuplicateInstanceOfProcess;
using ::perception::kMaxInterruptReadBytes;
using ::perception::ProcessId;
using ::perception::Read8BitsFromPort;
using ::perception::RegisterInterruptHandlerLoopOverStatusPortReadMaskedPort;
using ::perception::Write8BitsToPort;
using ::perception::devices::KeyboardDevice;
using ::perception::devices::KeyboardEvent;
using ::perception::devices::KeyboardListener;
using ::perception::devices::MouseButton;
using ::perception::devices::MouseButtonEvent;
using ::perception::devices::MouseClickEvent;
using ::perception::devices::MouseDevice;
using ::perception::devices::MouseListener;
using ::perception::devices::MousePositionEvent;
using ::perception::devices::RelativeMousePositionEvent;
using ::perception::window::WindowManager;

namespace {

// Maximum polling iterations when waiting for PS/2 controller readiness.
constexpr size_t kTimeout = 100000;

// PS/2 Set 1 extended scancode prefix byte.
constexpr uint8 kExtendedScancodePrefix = 0xE0;

// PS/2 command to set mouse sample rate.
constexpr uint8 kSetSampleRateCommand = 0xF3;

// PS/2 command to query device ID.
constexpr uint8 kGetDeviceIdCommand = 0xF2;

// PS/2 command to set default mouse parameters.
constexpr uint8 kSetDefaultsCommand = 0xF6;

// PS/2 command to enable mouse packet streaming.
constexpr uint8 kEnablePacketStreamingCommand = 0xF4;

// PS/2 device ID for standard IntelliMouse with vertical scroll wheel.
constexpr uint8 kIntelliMouseDeviceId = 3;

// PS/2 device ID for IntelliMouse Explorer with 5 buttons and scroll wheel.
constexpr uint8 kIntelliMouseExplorerDeviceId = 4;

// #define SYSTEM_KEY_TOGGLES_PROFILING

// The system key (set to Escape) to send to the window manager.
constexpr uint8 kSystemKeyDown = 1;

enum class MousePacketState {
  kAwaitingByte1,
  kAwaitingByte2,
  kAwaitingByte3,
  kAwaitingByte4
};

class PS2MouseDevice : public MouseDevice::Server {
 public:
  PS2MouseDevice()
      : packet_state_(MousePacketState::kAwaitingByte1),
        last_button_state_{false, false, false},
        has_scroll_wheel_(false),
        is_five_button_wheel_(false) {}

  virtual ~PS2MouseDevice() {
    if (mouse_captor_) mouse_captor_->MouseReleased(nullptr);
  }

  void SetHasScrollWheel(bool has_scroll_wheel, bool is_five_button_wheel) {
    has_scroll_wheel_ = has_scroll_wheel;
    is_five_button_wheel_ = is_five_button_wheel;
  }

  void HandleMouseInterrupt(uint8 val) {
    switch (packet_state_) {
      case MousePacketState::kAwaitingByte1:
        // The first byte must have bit 3 set. If not, the stream is out of
        // sync; stay in this state and ignore the byte.
        if ((val & (1 << 3)) == 0) return;
        mouse_byte_buffer_[0] = val;
        packet_state_ = MousePacketState::kAwaitingByte2;
        break;
      case MousePacketState::kAwaitingByte2:
        mouse_byte_buffer_[1] = val;
        packet_state_ = MousePacketState::kAwaitingByte3;
        break;
      case MousePacketState::kAwaitingByte3:
        if (has_scroll_wheel_) {
          mouse_byte_buffer_[2] = val;
          packet_state_ = MousePacketState::kAwaitingByte4;
        } else {
          ProcessMouseMessage(mouse_byte_buffer_[0], mouse_byte_buffer_[1], val,
                              0);
          packet_state_ = MousePacketState::kAwaitingByte1;
        }
        break;
      case MousePacketState::kAwaitingByte4:
        ProcessMouseMessage(mouse_byte_buffer_[0], mouse_byte_buffer_[1],
                            mouse_byte_buffer_[2], val);
        packet_state_ = MousePacketState::kAwaitingByte1;
        break;
    }
  }

  virtual Status SetMouseListener(
      const MouseListener::Client& listener) override {
    if (mouse_captor_) mouse_captor_->MouseReleased(nullptr);
    if (listener.IsValid()) {
      mouse_captor_ = std::make_unique<MouseListener::Client>(listener);
      mouse_captor_->MouseTakenCaptive(nullptr);
    } else {
      mouse_captor_.reset();
    }
    return Status::OK;
  }

 private:
  // Messages from the mouse come in 3 or 4 bytes. Buffer these until there are
  // enough bytes to process the message.
  MousePacketState packet_state_;
  uint8 mouse_byte_buffer_[3];

  // The last known state of the mouse buttons.
  bool last_button_state_[3];

  // Whether the mouse sends 4-byte packets with scroll wheel data.
  bool has_scroll_wheel_;

  // Whether the 4th byte uses 4-bit signed scroll plus extra button bits.
  bool is_five_button_wheel_;

  // The service to send mouse events to.
  std::unique_ptr<MouseListener::Client> mouse_captor_;

  // Processes the mouse message.
  void ProcessMouseMessage(uint8 status, uint8 offset_x, uint8 offset_y,
                           uint8 byte4) {
    int16 delta_x = 0;
    if (status & (1 << 6)) {
      std::cout << "X overflowed!" << std::endl;
    } else {
      delta_x = (int16)offset_x - (((int16)status << 4) & 0x100);
    }

    int16 delta_y = 0;
    if (status & (1 << 7)) {
      std::cout << "Y overflowed!" << std::endl;
    } else {
      delta_y = -(int16)offset_y + (((int16)status << 3) & 0x100);
    }

    if ((delta_x != 0 || delta_y != 0) && mouse_captor_) {
      RelativeMousePositionEvent message;
      message.delta_x = static_cast<float>(delta_x);
      message.delta_y = static_cast<float>(delta_y);
      mouse_captor_->MouseMove(message, nullptr);
    }

    if (has_scroll_wheel_ && mouse_captor_) {
      int8 scroll_z = is_five_button_wheel_
                          ? (static_cast<int8>(byte4 << 4) >> 4)
                          : static_cast<int8>(byte4);
      if (scroll_z != 0) {
        RelativeMousePositionEvent scroll_msg;
        scroll_msg.delta_x = 0.0f;
        scroll_msg.delta_y = static_cast<float>(scroll_z);
        mouse_captor_->MouseScroll(scroll_msg, nullptr);
      }
    }

    // Read the left, middle, right buttons.
    bool buttons[3] = {(status & (1)) == 1, (status & (1 << 2)) == 4,
                       (status & (1 << 1)) == 2};

    for (int button_index : {0, 1, 2}) {
      if (buttons[button_index] != last_button_state_[button_index]) {
        last_button_state_[button_index] = buttons[button_index];
        if (mouse_captor_) {
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
          mouse_captor_->MouseButton(message, nullptr);
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
      // Tell the captor that the keyboard has to be released.
      keyboard_captor_->KeyboardReleased(nullptr);
    }
  }

  void HandleKeyboardInterrupt(uint8 val) {
    if (val == kExtendedScancodePrefix) return;

    if (val == kSystemKeyDown) {
#ifdef SYSTEM_KEY_TOGGLES_PROFILING
      static bool profiling_enabled = false;
      profiling_enabled = !profiling_enabled;
      if (profiling_enabled) {
        ::perception::EnableProfiling();
      } else {
        ::perception::DisableAndOutputProfiling();
      }
#endif

      // The system key was pressed. Notify the window manager.
      auto window_manager = FindFirstInstanceOfService<WindowManager>();
      if (window_manager) window_manager->SystemButtonPushed(nullptr);
    }

    if (!keyboard_captor_)
      // No one to send the keyboard event to.
      return;

    uint8 key = val & 127;
    if ((val & 128) == 0) {
      // Send our captor a message that the key was pressed down.
      KeyboardEvent message;
      message.key = key;
      keyboard_captor_->KeyDown(message, nullptr);
    } else {
      // Send our captor a message that the key was released.
      KeyboardEvent message;
      message.key = key;
      keyboard_captor_->KeyUp(message, nullptr);
    }
  }

  virtual Status SetKeyboardListener(
      const KeyboardListener::Client& listener) override {
    if (keyboard_captor_) {
      // Let the old captor know the keyboard has escaped.
      keyboard_captor_->KeyboardReleased(nullptr);
    }
    if (listener.IsValid()) {
      keyboard_captor_ = std::make_unique<KeyboardListener::Client>(listener);
      // Let our captor know they have taken the keybord captive.
      keyboard_captor_->KeyboardTakenCaptive(nullptr);
    } else {
      keyboard_captor_.reset();
    }
    return Status::OK;
  }

 private:
  // The service to send keyboard events to.
  std::unique_ptr<KeyboardListener::Client> keyboard_captor_;
};

// Global instance of the mouse device.
std::unique_ptr<PS2MouseDevice> mouse_device;

// Global instance of the keyboard device.
std::unique_ptr<PS2KeyboardDevice> keyboard_device;

void InterruptHandler(const uint8* bytes) {
  // Loop over each byte until the status is NULL or there are no more bytes.
  for (int offset = 0; offset < kMaxInterruptReadBytes && bytes[offset] != 0;
       offset += 2) {
    uint8 status = bytes[offset];
    if ((status & (1 << 6)) /** Parity error. */ ||
        (status & (1 << 7)) /** General Timeout Error. */) {
      continue;
    }

    if (status & (1 << 5)) {
      if (mouse_device != nullptr)
        mouse_device->HandleMouseInterrupt(bytes[offset + 1]);
    } else {
      if (keyboard_device != nullptr)
        keyboard_device->HandleKeyboardInterrupt(bytes[offset + 1]);
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

void SetMouseSampleRate(uint8 rate) {
  MouseWrite(kSetSampleRateCommand);
  (void)MouseRead();
  MouseWrite(rate);
  (void)MouseRead();
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
  MouseWrite(kSetDefaultsCommand);
  (void)MouseRead();

  // Send the IntelliMouse magic sample rate sequence (200, 100, 80) to enable
  // the vertical scroll wheel.
  SetMouseSampleRate(200);
  SetMouseSampleRate(100);
  SetMouseSampleRate(80);

  MouseWrite(kGetDeviceIdCommand);
  (void)MouseRead();
  uint8 device_id = MouseRead();

  if (device_id == kIntelliMouseDeviceId ||
      device_id == kIntelliMouseExplorerDeviceId)
    mouse_device->SetHasScrollWheel(true,
                                    device_id == kIntelliMouseExplorerDeviceId);

  // Enable packet streaming.
  MouseWrite(kEnablePacketStreamingCommand);
  (void)MouseRead();
}

}  // namespace

int main(int argc, char* argv[]) {
  if (IsDuplicateInstanceOfProcess()) return 0;

  bool enable_keyboard = false;
  bool enable_mouse = false;

  for (int i = 1; i < argc; i++) {
    std::string_view arg(argv[i]);
    if (arg == "keyboard") enable_keyboard = true;
    if (arg == "mouse") enable_mouse = true;
  }

  if (!enable_keyboard && !enable_mouse) {
    return 0;
  }

  if (enable_keyboard) {
    std::cout << "Initializing PS/2 Keyboard..." << std::endl;
    keyboard_device = std::make_unique<PS2KeyboardDevice>();
  }

  if (enable_mouse) {
    std::cout << "Initializing PS/2 Mouse..." << std::endl;
    mouse_device = std::make_unique<PS2MouseDevice>();
    InitializePS2Controller();
  }

  if (enable_keyboard) {
    RegisterInterruptHandlerLoopOverStatusPortReadMaskedPort(
        /*irq=*/1, /*status_port=*/0x64, /*mask=*/1, /*read_port=*/0x60,
        InterruptHandler);
  }

  if (enable_mouse) {
    RegisterInterruptHandlerLoopOverStatusPortReadMaskedPort(
        /*irq=*/12, /*status_port=*/0x64, /*mask=*/1, /*read_port=*/0x60,
        InterruptHandler);
  }

  perception::HandOverControl();
  return 0;
}
