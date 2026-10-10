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

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "kitty_graphics.h"
#include "kitty_input.h"
#include "terminal_buffer.h"
#include "types.h"

// Callbacks invoked by EscapeParser when terminal sequences interact with the
// host window, clipboard, or input pipe.
struct EscapeParserCallbacks {
  std::function<void(std::string_view)> send_response;
  std::function<void(std::string_view)> on_title_changed;
  std::function<void(uint32)> on_background_color_changed;
  std::function<void()> on_cursor_shape_changed;
  std::function<void(std::string_view)> on_set_clipboard;
  std::function<std::string()> on_get_clipboard;
  std::function<void()> on_sync_output_ended;
};

// Streaming state-machine parser for UTF-8 text, VT100/ANSI CSI escape
// sequences, SGR attributes, OSC commands, Sixel DCS images, and Kitty
// Graphics/Keyboard/Text-Sizing/Pointer-Shape/Color-Stack protocols.
class EscapeParser {
 public:
  // Constructs an EscapeParser operating on `buffer`, `kitty_graphics`, and `kitty_input`.
  EscapeParser(TerminalBuffer& buffer, KittyGraphics& kitty_graphics,
               KittyInput& kitty_input,
               const EscapeParserCallbacks& callbacks = {});

  // Resets the parser state machine to the ground state.
  void Reset();

  // Updates the cell pixel dimensions used for Sixel, Kitty Graphics, and XTWINOPS reports.
  void SetCellMetrics(float cell_width, float cell_height);

  // Feeds a chunk of raw bytes from the child process into the state machine.
  void Feed(std::string_view data);

 private:
  // Internal state of the escape sequence state machine.
  enum class State {
    GROUND,
    UTF8,
    ESCAPE,
    ESC_CHARSET,
    ESC_HASH,
    CSI,
    OSC,
    DCS,
    APC,
    PM_SOS,
  };

  // Represents a single semicolon-separated CSI parameter and any colon subparameters.
  struct CsiParam {
    std::vector<int> subparams;
  };

  // Processes a single byte in the state machine.
  void ProcessByte(uint8 byte);

  // Handles C0 control characters (0x00..0x1F).
  void HandleControlChar(uint8 byte);

  // Dispatches a completed CSI sequence.
  void DispatchCsi(char final_byte);

  // Dispatches a DEC private mode set (`?h`) or reset (`?l`) sequence.
  void DispatchDecPrivateMode(const std::vector<CsiParam>& params, bool enable);

  // Dispatches an SGR (`CSI ... m`) attribute sequence.
  void DispatchSgr(const std::vector<CsiParam>& params);

  // Dispatches a completed OSC sequence (`ESC ] ... ST`).
  void DispatchOsc(std::string_view payload);

  // Dispatches a completed DCS sequence (`ESC P ... ST`), such as Sixel graphics.
  void DispatchDcs(std::string_view payload);

  // Dispatches a completed APC sequence (`ESC _ ... ST`), such as Kitty Graphics.
  void DispatchApc(std::string_view payload);

  // Parses the accumulated `csi_param_Buffer_` into structured `CsiParam` entries.
  std::vector<CsiParam> ParseCsiParams() const;

  // Sends a string response back to the child's stdin pipe if `send_response` is set.
  void SendResponse(std::string_view response) const;

  TerminalBuffer& buffer_;
  KittyGraphics& kitty_graphics_;
  KittyInput& kitty_input_;
  EscapeParserCallbacks callbacks_;

  float cell_width_;
  float cell_height_;

  State state_;
  char32_t utf8_codepoint_;
  int utf8_remaining_;
  char32_t utf8_min_codepoint_;
  char32_t last_printed_codepoint_;

  char esc_charset_target_;
  char csi_prefix_;
  char csi_intermediate_;
  std::string csi_param_buffer_;

  State string_return_state_;
  bool string_saw_esc_;
  std::string string_payload_;
};
