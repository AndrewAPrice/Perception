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

#include "escape_parser.h"

#include <algorithm>
#include <cmath>

#include "sixel_decoder.h"

namespace {

// Default character cell width in pixels before layout measurement.
constexpr float kDefaultCellWidth = 8.0f;

// Default character cell height in pixels before layout measurement.
constexpr float kDefaultCellHeight = 16.0f;

// Maximum length in bytes of an accumulated CSI parameter string.
constexpr size_t kMaxCsiParamBufferBytes = 256;

// Maximum length in bytes of an accumulated OSC, DCS, or APC string payload.
constexpr size_t kMaxStringPayloadBytes = 4 * 1024 * 1024;

// Maximum repeat count for the CSI b (REP) sequence.
constexpr int kMaxRepeatCharCount = 4096;

// Base64 alphabet table used for OSC 52 clipboard encoding.
constexpr std::string_view kBase64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Encodes raw bytes as a standard base64 string.
std::string Base64Encode(std::string_view input) {
  std::string out;
  out.reserve(((input.size() + 2) / 3) * 4);
  uint32 val = 0;
  int valb = -6;
  for ( char c : input) {
    val = (val << 8) + static_cast<uint8>(c);
    valb += 8;
    while (valb >= 0) {
      out.push_back(kBase64Alphabet[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6)
    out.push_back(kBase64Alphabet[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4 != 0)
    out.push_back('=');
  return out;
}

// Decodes a base64 string into a plain string.
std::string Base64DecodeString(std::string_view encoded) {
  std::string out;
  out.reserve((encoded.size() * 3) / 4);
  uint32 accum = 0;
  int bits = 0;
  for (char c : encoded) {
    int val = -1;
    if (c >= 'A' && c <= 'Z') {
      val = c - 'A';
    } else if (c >= 'a' && c <= 'z') {
      val = c - 'a' + 26;
    } else if (c >= '0' && c <= '9') {
      val = c - '0' + 52;
    } else if (c == '+') {
      val = 62;
    } else if (c == '/') {
      val = 63;
    } else if (c == '=') {
      break;
    } else {
      continue;
    }
    accum = (accum << 6) | static_cast<uint32>(val);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((accum >> bits) & 0xFF));
    }
  }
  return out;
}

// Parses a single hex digit (`0-9`, `a-f`, `A-F`), or returns -1 if invalid.
int ParseHexDigit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

// Parses a hex byte or component from `sv` (1 to 4 hex digits) scaled to 0..255.
int ParseHexComponent(std::string_view sv) {
  if (sv.empty() || sv.size() > 4)
    return -1;
  int val = 0;
  for (char c : sv) {
    int d = ParseHexDigit(c);
    if (d < 0)
      return -1;
    val = (val << 4) | d;
  }
  if (sv.size() == 1)
    return val * 17;
  if (sv.size() == 2)
    return val;
  if (sv.size() == 3)
    return val >> 4;
  return val >> 8;
}

// Parses an XParseColor / OSC color specification (`#RRGGBB` or `rgb:r/g/b`).
bool ParseOscColor(std::string_view spec, uint32& color_out) {
  if (spec.empty())
    return false;
  if (spec[0] == '#') {
    std::string_view hex = spec.substr(1);
    if (hex.size() == 6) {
      int r = ParseHexComponent(hex.substr(0, 2));
      int g = ParseHexComponent(hex.substr(2, 2));
      int b = ParseHexComponent(hex.substr(4, 2));
      if (r >= 0 && g >= 0 && b >= 0) {
        color_out = 0xFF000000u | (static_cast<uint32>(r) << 16) |
                    (static_cast<uint32>(g) << 8) | static_cast<uint32>(b);
        return true;
      }
    } else if (hex.size() == 3) {
      int r = ParseHexComponent(hex.substr(0, 1));
      int g = ParseHexComponent(hex.substr(1, 1));
      int b = ParseHexComponent(hex.substr(2, 1));
      if (r >= 0 && g >= 0 && b >= 0) {
        color_out = 0xFF000000u | (static_cast<uint32>(r) << 16) |
                    (static_cast<uint32>(g) << 8) | static_cast<uint32>(b);
        return true;
      }
    }
    return false;
  }
  if (spec.size() > 4 && spec.substr(0, 4) == "rgb:") {
    std::string_view comps = spec.substr(4);
    size_t s1 = comps.find('/');
    if (s1 == std::string_view::npos)
      return false;
    size_t s2 = comps.find('/', s1 + 1);
    if (s2 == std::string_view::npos)
      return false;
    int r = ParseHexComponent(comps.substr(0, s1));
    int g = ParseHexComponent(comps.substr(s1 + 1, s2 - s1 - 1));
    int b = ParseHexComponent(comps.substr(s2 + 1));
    if (r >= 0 && g >= 0 && b >= 0) {
      color_out = 0xFF000000u | (static_cast<uint32>(r) << 16) |
                  (static_cast<uint32>(g) << 8) | static_cast<uint32>(b);
      return true;
    }
  }
  return false;
}

// Formats a 2-digit lowercase hex byte repeated twice (`RRRR`) for OSC color replies.
void AppendHex16(uint8 byte, std::string& out) {
  constexpr char kHex[] = "0123456789abcdef";
  char hi = kHex[(byte >> 4) & 0x0F];
  char lo = kHex[byte & 0x0F];
  out.push_back(hi);
  out.push_back(lo);
  out.push_back(hi);
  out.push_back(lo);
}

// Formats an OSC 10/11/12 color query response (`\x1b]<code_num>;rgb:rrrr/gggg/bbbb\x1b\\`).
std::string FormatOscColorResponse(int osc_code, uint32 color) {
  std::string out = "\x1b]" + std::to_string(osc_code) + ";rgb:";
  AppendHex16(static_cast<uint8>((color >> 16) & 0xFF), out);
  out.push_back('/');
  AppendHex16(static_cast<uint8>((color >> 8) & 0xFF), out);
  out.push_back('/');
  AppendHex16(static_cast<uint8>(color & 0xFF), out);
  out += "\x1b\\";
  return out;
}

// Parses a non-negative integer from `sv`, returning `default_val` if empty.
int ParseNonNegativeInt(std::string_view sv, int default_val) {
  if (sv.empty())
    return default_val;
  int val = 0;
  bool any = false;
  for (char c : sv) {
    if (c >= '0' && c <= '9') {
      val = val * 10 + (c - '0');
      any = true;
    } else {
      break;
    }
  }
  return any ? val : default_val;
}

}  // namespace

EscapeParser::EscapeParser(TerminalBuffer& buffer,
                           KittyGraphics& kitty_graphics,
                           KittyInput& kitty_input,
                           const EscapeParserCallbacks& callbacks)
    : buffer_(buffer),
      kitty_graphics_(kitty_graphics),
      kitty_input_(kitty_input),
      callbacks_(callbacks),
      cell_width_(kDefaultCellWidth),
      cell_height_(kDefaultCellHeight) {
  Reset();
}

void EscapeParser::Reset() {
  state_ = State::GROUND;
  utf8_codepoint_ = 0;
  utf8_remaining_ = 0;
  utf8_min_codepoint_ = 0;
  last_printed_codepoint_ = U' ';
  esc_charset_target_ = '(';
  csi_prefix_ = 0;
  csi_intermediate_ = 0;
  csi_param_buffer_.clear();
  string_return_state_ = State::GROUND;
  string_saw_esc_ = false;
  string_payload_.clear();
}

void EscapeParser::SetCellMetrics(float cell_width, float cell_height) {
  cell_width_ = std::max(1.0f, cell_width);
  cell_height_ = std::max(1.0f, cell_height);
}

void EscapeParser::Feed(std::string_view data) {
  for (char c : data)
    ProcessByte(static_cast<uint8>(c));
}

void EscapeParser::ProcessByte(uint8 byte) {
  if (state_ == State::OSC || state_ == State::DCS || state_ == State::APC ||
      state_ == State::PM_SOS) {
    if (string_saw_esc_) {
      string_saw_esc_ = false;
      if (byte == '\\') {
        State finished_state = state_;
        state_ = State::GROUND;
        if (finished_state == State::OSC) {
          DispatchOsc(string_payload_);
        } else if (finished_state == State::DCS) {
          DispatchDcs(string_payload_);
        } else if (finished_state == State::APC) {
          DispatchApc(string_payload_);
        }
        string_payload_.clear();
        return;
      }
      if (byte == 0x1B) {
        if (string_payload_.size() < kMaxStringPayloadBytes)
          string_payload_.push_back('\x1b');
        string_saw_esc_ = true;
        return;
      }
      if (string_payload_.size() < kMaxStringPayloadBytes)
        string_payload_.push_back('\x1b');
    }

    if (byte == 0x1B) {
      string_saw_esc_ = true;
      return;
    }
    if (byte == 0x07 && state_ == State::OSC) {
      state_ = State::GROUND;
      DispatchOsc(string_payload_);
      string_payload_.clear();
      return;
    }
    if (byte == 0x9C) {
      State finished_state = state_;
      state_ = State::GROUND;
      if (finished_state == State::OSC) {
        DispatchOsc(string_payload_);
      } else if (finished_state == State::DCS) {
        DispatchDcs(string_payload_);
      } else if (finished_state == State::APC) {
        DispatchApc(string_payload_);
      }
      string_payload_.clear();
      return;
    }
    if (byte == 0x18 || byte == 0x1A) {
      string_payload_.clear();
      state_ = State::GROUND;
      return;
    }
    if (string_payload_.size() < kMaxStringPayloadBytes)
      string_payload_.push_back(static_cast<char>(byte));
    return;
  }

  if (byte == 0x1B) {
    state_ = State::ESCAPE;
    utf8_remaining_ = 0;
    return;
  }
  if (byte == 0x18 || byte == 0x1A) {
    state_ = State::GROUND;
    utf8_remaining_ = 0;
    return;
  }
  if (byte < 0x20) {
    HandleControlChar(byte);
    return;
  }
  if (byte == 0x7F)
    return;

  switch (state_) {
    case State::GROUND: {
      if (byte < 0x80) {
        char32_t cp = static_cast<char32_t>(byte);
        last_printed_codepoint_ = cp;
        buffer_.PutChar(cp);
      } else if ((byte & 0xE0) == 0xC0) {
        utf8_codepoint_ = byte & 0x1F;
        utf8_remaining_ = 1;
        utf8_min_codepoint_ = 0x80;
        state_ = State::UTF8;
      } else if ((byte & 0xF0) == 0xE0) {
        utf8_codepoint_ = byte & 0x0F;
        utf8_remaining_ = 2;
        utf8_min_codepoint_ = 0x800;
        state_ = State::UTF8;
      } else if ((byte & 0xF8) == 0xF0) {
        utf8_codepoint_ = byte & 0x07;
        utf8_remaining_ = 3;
        utf8_min_codepoint_ = 0x10000;
        state_ = State::UTF8;
      }
      break;
    }

    case State::UTF8: {
      if ((byte & 0xC0) == 0x80) {
        utf8_codepoint_ = (utf8_codepoint_ << 6) | (byte & 0x3F);
        utf8_remaining_--;
        if (utf8_remaining_ == 0) {
          char32_t cp =
              (utf8_codepoint_ >= utf8_min_codepoint_ &&
               utf8_codepoint_ <= 0x10FFFF &&
               (utf8_codepoint_ < 0xD800 || utf8_codepoint_ > 0xDFFF))
                  ? utf8_codepoint_
                  : 0xFFFD;
          state_ = State::GROUND;
          last_printed_codepoint_ = cp;
          buffer_.PutChar(cp);
        }
      } else {
        state_ = State::GROUND;
        utf8_remaining_ = 0;
        ProcessByte(byte);
      }
      break;
    }

    case State::ESCAPE: {
      char ch = static_cast<char>(byte);
      switch (ch) {
        case '[':
          state_ = State::CSI;
          csi_prefix_ = 0;
          csi_intermediate_ = 0;
          csi_param_buffer_.clear();
          break;
        case ']':
          state_ = State::OSC;
          string_saw_esc_ = false;
          string_payload_.clear();
          break;
        case 'P':
          state_ = State::DCS;
          string_saw_esc_ = false;
          string_payload_.clear();
          break;
        case '_':
          state_ = State::APC;
          string_saw_esc_ = false;
          string_payload_.clear();
          break;
        case '^':
        case 'X':
          state_ = State::PM_SOS;
          string_saw_esc_ = false;
          string_payload_.clear();
          break;
        case '(':
        case ')':
        case '*':
        case '+':
          esc_charset_target_ = ch;
          state_ = State::ESC_CHARSET;
          break;
        case '#':
          state_ = State::ESC_HASH;
          break;
        case '7':
          buffer_.SaveCursor();
          state_ = State::GROUND;
          break;
        case '8':
          buffer_.RestoreCursor();
          state_ = State::GROUND;
          break;
        case 'D':
          buffer_.Index();
          state_ = State::GROUND;
          break;
        case 'E':
          buffer_.NewLine(true);
          state_ = State::GROUND;
          break;
        case 'M':
          buffer_.ReverseIndex();
          state_ = State::GROUND;
          break;
        case 'c':
          buffer_.Reset();
          kitty_graphics_.Reset();
          kitty_input_.Reset();
          if (callbacks_.on_background_color_changed) {
            callbacks_.on_background_color_changed(
                buffer_.GetDefaultBackgroundColor());
          }
          if (callbacks_.on_cursor_shape_changed)
            callbacks_.on_cursor_shape_changed();
          state_ = State::GROUND;
          break;
        default:
          state_ = State::GROUND;
          break;
      }
      break;
    }

    case State::ESC_CHARSET: {
      if (esc_charset_target_ == '(')
        buffer_.SetDecSpecialGraphics(byte == '0');
      state_ = State::GROUND;
      break;
    }

    case State::ESC_HASH: {
      if (byte == '8') {
        buffer_.SetOriginMode(false);
        buffer_.SetCursorPos(0, 0);
        for (int r = 0; r < buffer_.Rows(); ++r) {
          buffer_.SetRawCursorPos(r, 0);
          for (int c = 0; c < buffer_.Cols(); ++c)
            buffer_.PutChar(U'E');
        }
        buffer_.SetCursorPos(0, 0);
      }
      state_ = State::GROUND;
      break;
    }

    case State::CSI: {
      if (byte >= 0x30 && byte <= 0x3F) {
        if ((byte >= '<' && byte <= '?') && csi_param_buffer_.empty() &&
            csi_prefix_ == 0) {
          csi_prefix_ = static_cast<char>(byte);
        } else if (csi_param_buffer_.size() < kMaxCsiParamBufferBytes) {
          csi_param_buffer_.push_back(static_cast<char>(byte));
        }
      } else if (byte >= 0x20 && byte <= 0x2F) {
        csi_intermediate_ = static_cast<char>(byte);
      } else if (byte >= 0x40 && byte <= 0x7E) {
        DispatchCsi(static_cast<char>(byte));
        state_ = State::GROUND;
      } else {
        state_ = State::GROUND;
      }
      break;
    }

    default:
      state_ = State::GROUND;
      break;
  }
}

void EscapeParser::HandleControlChar(uint8 byte) {
  switch (byte) {
    case 0x08:
      buffer_.Backspace();
      break;
    case 0x09:
      buffer_.Tab();
      break;
    case 0x0A:
    case 0x0B:
    case 0x0C:
      buffer_.NewLine(true);
      break;
    case 0x0D:
      buffer_.CarriageReturn();
      break;
    case 0x0E:
      buffer_.SetDecSpecialGraphics(true);
      break;
    case 0x0F:
      buffer_.SetDecSpecialGraphics(false);
      break;
    default:
      break;
  }
}

std::vector<EscapeParser::CsiParam> EscapeParser::ParseCsiParams() const {
  std::vector<CsiParam> params;
  if (csi_param_buffer_.empty())
    return params;

  std::string_view sv(csi_param_buffer_);
  size_t pos = 0;
  while (pos <= sv.size()) {
    size_t semi = sv.find(';', pos);
    std::string_view token =
        (semi == std::string_view::npos) ? sv.substr(pos)
                                         : sv.substr(pos, semi - pos);
    CsiParam param;
    size_t sub_pos = 0;
    while (sub_pos <= token.size()) {
      size_t colon = token.find(':', sub_pos);
      std::string_view sub_tok =
          (colon == std::string_view::npos)
              ? token.substr(sub_pos)
              : token.substr(sub_pos, colon - sub_pos);
      param.subparams.push_back(ParseNonNegativeInt(sub_tok, -1));
      if (colon == std::string_view::npos)
        break;
      sub_pos = colon + 1;
    }
    params.push_back(std::move(param));
    if (semi == std::string_view::npos)
      break;
    pos = semi + 1;
  }
  return params;
}

void EscapeParser::DispatchCsi(char final_byte) {
  auto params = ParseCsiParams();
  auto get_param = [&](size_t idx, int default_val) -> int {
    if (idx >= params.size() || params[idx].subparams.empty())
      return default_val;
    int v = params[idx].subparams[0];
    return v < 0 ? default_val : v;
  };

  if (final_byte == 'u') {
    if (csi_prefix_ == '>') {
      uint32 flags = static_cast<uint32>(get_param(0, 0));
      kitty_input_.PushKeyboardFlags(flags, buffer_.IsAlternateScreen());
      return;
    }
    if (csi_prefix_ == '<') {
      int count = get_param(0, 1);
      kitty_input_.PopKeyboardFlags(count, buffer_.IsAlternateScreen());
      return;
    }
    if (csi_prefix_ == '=') {
      uint32 flags = static_cast<uint32>(get_param(0, 0));
      int mode = get_param(1, 1);
      kitty_input_.SetKeyboardFlags(flags, mode, buffer_.IsAlternateScreen());
      return;
    }
    if (csi_prefix_ == '?') {
      SendResponse(
          kitty_input_.FormatKeyboardQueryResponse(buffer_.IsAlternateScreen()));
      return;
    }
    if (csi_prefix_ == 0 && csi_intermediate_ == 0) {
      buffer_.RestoreCursor();
      return;
    }
  }

  if (csi_prefix_ == '?' && (final_byte == 'h' || final_byte == 'l')) {
    DispatchDecPrivateMode(params, final_byte == 'h');
    return;
  }

  if (csi_prefix_ == '>' && final_byte == 'c') {
    SendResponse("\x1b[>1;10;0c");
    return;
  }

  if (csi_intermediate_ == ' ' && final_byte == 'q') {
    buffer_.SetCursorStyle(get_param(0, 0));
    if (callbacks_.on_cursor_shape_changed)
      callbacks_.on_cursor_shape_changed();
    return;
  }

  if (csi_prefix_ != 0 || csi_intermediate_ != 0)
    return;

  switch (final_byte) {
    case 'A':
      buffer_.MoveCursorRelative(-std::max(1, get_param(0, 1)), 0);
      break;
    case 'B':
      buffer_.MoveCursorRelative(std::max(1, get_param(0, 1)), 0);
      break;
    case 'C':
      buffer_.MoveCursorRelative(0, std::max(1, get_param(0, 1)));
      break;
    case 'D':
      buffer_.MoveCursorRelative(0, -std::max(1, get_param(0, 1)));
      break;
    case 'E':
      buffer_.CarriageReturn();
      buffer_.MoveCursorRelative(std::max(1, get_param(0, 1)), 0);
      break;
    case 'F':
      buffer_.CarriageReturn();
      buffer_.MoveCursorRelative(-std::max(1, get_param(0, 1)), 0);
      break;
    case 'G':
    case '`':
      buffer_.SetRawCursorPos(buffer_.CursorRow(),
                              std::max(1, get_param(0, 1)) - 1);
      break;
    case 'H':
    case 'f':
      buffer_.SetCursorPos(std::max(1, get_param(0, 1)) - 1,
                           std::max(1, get_param(1, 1)) - 1);
      break;
    case 'J':
      buffer_.EraseInDisplay(get_param(0, 0));
      break;
    case 'K':
      buffer_.EraseInLine(get_param(0, 0));
      break;
    case 'L':
      buffer_.InsertLines(std::max(1, get_param(0, 1)));
      break;
    case 'M':
      buffer_.DeleteLines(std::max(1, get_param(0, 1)));
      break;
    case 'P':
      buffer_.DeleteChars(std::max(1, get_param(0, 1)));
      break;
    case '@':
      buffer_.InsertChars(std::max(1, get_param(0, 1)));
      break;
    case 'X':
      buffer_.EraseChars(std::max(1, get_param(0, 1)));
      break;
    case 'S':
      buffer_.ScrollUp(std::max(1, get_param(0, 1)));
      break;
    case 'T':
      buffer_.ScrollDown(std::max(1, get_param(0, 1)));
      break;
    case 'd':
      buffer_.SetCursorPos(std::max(1, get_param(0, 1)) - 1,
                           buffer_.CursorCol());
      break;
    case 'b': {
      int count = std::clamp(get_param(0, 1), 1, kMaxRepeatCharCount);
      for (int i = 0; i < count; ++i)
        buffer_.PutChar(last_printed_codepoint_);
      break;
    }
    case 'r': {
      int p0 = get_param(0, 0);
      int p1 = get_param(1, 0);
      int top = (p0 <= 0) ? 0 : (p0 - 1);
      int bottom = (p1 <= 0) ? (buffer_.Rows() - 1) : (p1 - 1);
      buffer_.SetScrollRegion(top, bottom);
      break;
    }
    case 's':
      buffer_.SaveCursor();
      break;
    case 'm':
      DispatchSgr(params);
      break;
    case 'n': {
      int mode = get_param(0, 0);
      if (mode == 5) {
        SendResponse("\x1b[0n");
      } else if (mode == 6) {
        int report_row = buffer_.IsOriginMode()
                             ? (buffer_.CursorRow() - buffer_.ScrollTop() + 1)
                             : (buffer_.CursorRow() + 1);
        std::string resp = "\x1b[" + std::to_string(report_row) + ";" +
                           std::to_string(buffer_.CursorCol() + 1) + "R";
        SendResponse(resp);
      }
      break;
    }
    case 'c':
      SendResponse("\x1b[?62;4c");
      break;
    case 't': {
      int op = get_param(0, 0);
      int px_w = static_cast<int>(std::round(buffer_.Cols() * cell_width_));
      int px_h = static_cast<int>(std::round(buffer_.Rows() * cell_height_));
      if (op == 14) {
        SendResponse("\x1b[4;" + std::to_string(px_h) + ";" +
                     std::to_string(px_w) + "t");
      } else if (op == 16) {
        SendResponse("\x1b[6;" +
                     std::to_string(static_cast<int>(std::round(cell_height_))) +
                     ";" +
                     std::to_string(static_cast<int>(std::round(cell_width_))) +
                     "t");
      } else if (op == 18) {
        SendResponse("\x1b[8;" + std::to_string(buffer_.Rows()) + ";" +
                     std::to_string(buffer_.Cols()) + "t");
      }
      break;
    }
    default:
      break;
  }
}

void EscapeParser::DispatchDecPrivateMode(const std::vector<CsiParam>& params,
                                          bool enable) {
  for (const auto& param : params) {
    if (param.subparams.empty() || param.subparams[0] < 0)
      continue;
    int mode = param.subparams[0];
    switch (mode) {
      case 1:
        buffer_.SetApplicationCursorKeys(enable);
        break;
      case 6:
        buffer_.SetOriginMode(enable);
        break;
      case 7:
        buffer_.SetAutoWrap(enable);
        break;
      case 12:
        buffer_.SetCursorBlinking(enable);
        if (callbacks_.on_cursor_shape_changed)
          callbacks_.on_cursor_shape_changed();
        break;
      case 25:
        buffer_.SetCursorVisible(enable);
        break;
      case 47:
        buffer_.SetAlternateScreen(enable, false, false);
        break;
      case 1000:
      case 1002:
      case 1003:
        if (enable) {
          buffer_.SetMouseTrackingMode(mode);
        } else if (buffer_.MouseTrackingMode() == mode) {
          buffer_.SetMouseTrackingMode(0);
        }
        break;
      case 1004:
        buffer_.SetFocusReporting(enable);
        break;
      case 1006:
      case 1016:
        if (enable) {
          buffer_.SetMouseEncodingMode(mode);
        } else if (buffer_.MouseEncodingMode() == mode) {
          buffer_.SetMouseEncodingMode(0);
        }
        break;
      case 1047:
        buffer_.SetAlternateScreen(enable, false, true);
        break;
      case 1048:
        if (enable) {
          buffer_.SaveCursor();
        } else {
          buffer_.RestoreCursor();
        }
        break;
      case 1049:
        buffer_.SetAlternateScreen(enable, true, true);
        break;
      case 2004:
        buffer_.SetBracketedPaste(enable);
        break;
      case 2026: {
        bool was_sync = buffer_.IsSynchronizedOutput();
        buffer_.SetSynchronizedOutput(enable);
        if (was_sync && !enable && callbacks_.on_sync_output_ended)
          callbacks_.on_sync_output_ended();
        break;
      }
      case 2048: {
        buffer_.SetInBandResize(enable);
        if (enable) {
          int px_w = static_cast<int>(std::round(buffer_.Cols() * cell_width_));
          int px_h =
              static_cast<int>(std::round(buffer_.Rows() * cell_height_));
          SendResponse("\x1b[48;" + std::to_string(buffer_.Rows()) + ";" +
                       std::to_string(buffer_.Cols()) + ";" +
                       std::to_string(px_h) + ";" + std::to_string(px_w) + "t");
        }
        break;
      }
      default:
        break;
    }
  }
}

void EscapeParser::DispatchSgr(const std::vector<CsiParam>& params) {
  if (params.empty()) {
    buffer_.ResetPen();
    return;
  }

  Cell& pen = buffer_.CurrentPen();
  for (size_t i = 0; i < params.size(); ++i) {
    const auto& sub = params[i].subparams;
    int code = (sub.empty() || sub[0] < 0) ? 0 : sub[0];

    switch (code) {
      case 0:
        buffer_.ResetPen();
        break;
      case 1:
        pen.flags |= kCellFlagBold;
        break;
      case 2:
        pen.flags |= kCellFlagDim;
        break;
      case 3:
        pen.flags |= kCellFlagItalic;
        break;
      case 4: {
        int style = (sub.size() >= 2 && sub[1] >= 0) ? sub[1] : 1;
        pen.underline_style = static_cast<uint8>(std::clamp(style, 0, 5));
        break;
      }
      case 5:
      case 6:
        pen.flags |= kCellFlagBlink;
        break;
      case 7:
        pen.flags |= kCellFlagInverse;
        break;
      case 8:
        pen.flags |= kCellFlagHidden;
        break;
      case 9:
        pen.flags |= kCellFlagStrikethrough;
        break;
      case 10:
      case 11:
      case 12:
      case 13:
      case 14:
        pen.font_family = static_cast<uint8>(code - 10);
        break;
      case 21:
        pen.underline_style = 2;
        break;
      case 22:
        pen.flags &= ~(kCellFlagBold | kCellFlagDim);
        break;
      case 23:
        pen.flags &= ~kCellFlagItalic;
        break;
      case 24:
        pen.underline_style = 0;
        break;
      case 25:
        pen.flags &= ~kCellFlagBlink;
        break;
      case 27:
        pen.flags &= ~kCellFlagInverse;
        break;
      case 28:
        pen.flags &= ~kCellFlagHidden;
        break;
      case 29:
        pen.flags &= ~kCellFlagStrikethrough;
        break;
      case 30:
      case 31:
      case 32:
      case 33:
      case 34:
      case 35:
      case 36:
      case 37:
        pen.fg_color = buffer_.GetAnsiColor(code - 30);
        break;
      case 39:
        pen.fg_color = buffer_.GetDefaultForegroundColor();
        break;
      case 40:
      case 41:
      case 42:
      case 43:
      case 44:
      case 45:
      case 46:
      case 47:
        pen.bg_color = buffer_.GetAnsiColor(code - 40);
        break;
      case 49:
        pen.bg_color = buffer_.GetDefaultBackgroundColor();
        break;
      case 59:
        pen.underline_color = 0;
        break;
      case 90:
      case 91:
      case 92:
      case 93:
      case 94:
      case 95:
      case 96:
      case 97:
        pen.fg_color = buffer_.GetAnsiColor(code - 90 + 8);
        break;
      case 100:
      case 101:
      case 102:
      case 103:
      case 104:
      case 105:
      case 106:
      case 107:
        pen.bg_color = buffer_.GetAnsiColor(code - 100 + 8);
        break;
      case 38:
      case 48:
      case 58: {
        uint32 parsed_color = 0;
        bool has_color = false;
        if (sub.size() >= 2 && sub[1] >= 0) {
          int mode = sub[1];
          if (mode == 5 && sub.size() >= 3 && sub[2] >= 0) {
            parsed_color = buffer_.GetColor256(sub[2]);
            has_color = true;
          } else if (mode == 2 && sub.size() >= 5) {
            size_t r_idx = (sub.size() >= 6) ? 3 : 2;
            int r = std::clamp(sub[r_idx] < 0 ? 0 : sub[r_idx], 0, 255);
            int g = std::clamp(sub[r_idx + 1] < 0 ? 0 : sub[r_idx + 1], 0, 255);
            int b = std::clamp(sub[r_idx + 2] < 0 ? 0 : sub[r_idx + 2], 0, 255);
            parsed_color = 0xFF000000u | (static_cast<uint32>(r) << 16) |
                           (static_cast<uint32>(g) << 8) |
                           static_cast<uint32>(b);
            has_color = true;
          }
        } else if (i + 1 < params.size() && !params[i + 1].subparams.empty()) {
          int mode = params[i + 1].subparams[0];
          if (mode == 5 && i + 2 < params.size() &&
              !params[i + 2].subparams.empty()) {
            int idx = std::max(0, params[i + 2].subparams[0]);
            parsed_color = buffer_.GetColor256(idx);
            has_color = true;
            i += 2;
          } else if (mode == 2 && i + 4 < params.size()) {
            int r = std::clamp(std::max(0, params[i + 2].subparams[0]), 0, 255);
            int g = std::clamp(std::max(0, params[i + 3].subparams[0]), 0, 255);
            int b = std::clamp(std::max(0, params[i + 4].subparams[0]), 0, 255);
            parsed_color = 0xFF000000u | (static_cast<uint32>(r) << 16) |
                           (static_cast<uint32>(g) << 8) |
                           static_cast<uint32>(b);
            has_color = true;
            i += 4;
          }
        }

        if (has_color) {
          if (code == 38) {
            pen.fg_color = parsed_color;
          } else if (code == 48) {
            pen.bg_color = parsed_color;
          } else {
            pen.underline_color = parsed_color;
          }
        }
        break;
      }
      default:
        break;
    }
  }
}

void EscapeParser::DispatchOsc(std::string_view payload) {
  size_t semi = payload.find(';');
  std::string_view cmd_str =
      (semi == std::string_view::npos) ? payload : payload.substr(0, semi);
  std::string_view rest =
      (semi == std::string_view::npos) ? "" : payload.substr(semi + 1);

  int cmd = ParseNonNegativeInt(cmd_str, -1);
  switch (cmd) {
    case 0:
    case 1:
    case 2:
      if (callbacks_.on_title_changed)
        callbacks_.on_title_changed(rest);
      break;

    case 8: {
      size_t url_sep = rest.find(';');
      if (url_sep != std::string_view::npos)
        buffer_.SetHyperlink(rest.substr(url_sep + 1));
      break;
    }

    case 10: {
      if (rest == "?") {
        SendResponse(
            FormatOscColorResponse(10, buffer_.GetDefaultForegroundColor()));
      } else {
        uint32 color = 0;
        if (ParseOscColor(rest, color))
          buffer_.SetDefaultForegroundColor(color);
      }
      break;
    }

    case 11: {
      if (rest == "?") {
        SendResponse(
            FormatOscColorResponse(11, buffer_.GetDefaultBackgroundColor()));
      } else {
        uint32 color = 0;
        if (ParseOscColor(rest, color)) {
          buffer_.SetDefaultBackgroundColor(color);
          if (callbacks_.on_background_color_changed)
            callbacks_.on_background_color_changed(color);
        }
      }
      break;
    }

    case 12: {
      if (rest == "?") {
        SendResponse(FormatOscColorResponse(12, buffer_.GetCursorColor()));
      } else {
        uint32 color = 0;
        if (ParseOscColor(rest, color))
          buffer_.SetCursorColor(color);
      }
      break;
    }

    case 22:
      kitty_input_.HandleOsc22(rest);
      if (callbacks_.on_cursor_shape_changed)
        callbacks_.on_cursor_shape_changed();
      break;

    case 52: {
      size_t data_sep = rest.find(';');
      std::string_view sel =
          (data_sep == std::string_view::npos) ? "c" : rest.substr(0, data_sep);
      std::string_view b64 = (data_sep == std::string_view::npos)
                                 ? rest
                                 : rest.substr(data_sep + 1);
      if (b64 == "?") {
        std::string clip =
            callbacks_.on_get_clipboard ? callbacks_.on_get_clipboard() : "";
        SendResponse("\x1b]52;" + std::string(sel) + ";" + Base64Encode(clip) +
                     "\x1b\\");
      } else if (callbacks_.on_set_clipboard) {
        callbacks_.on_set_clipboard(Base64DecodeString(b64));
      }
      break;
    }

    case 66: {
      size_t text_sep = rest.find(';');
      if (text_sep == std::string_view::npos)
        break;
      std::string_view meta = rest.substr(0, text_sep);
      std::string_view text = rest.substr(text_sep + 1);
      int scale = 1;
      int width = 0;
      size_t pos = 0;
      while (pos <= meta.size()) {
        size_t colon = meta.find(':', pos);
        std::string_view kv =
            (colon == std::string_view::npos)
                ? meta.substr(pos)
                : meta.substr(pos, colon - pos);
        size_t eq = kv.find('=');
        if (eq != std::string_view::npos) {
          std::string_view k = kv.substr(0, eq);
          std::string_view v = kv.substr(eq + 1);
          if (k == "s") {
            scale = ParseNonNegativeInt(v, 1);
          } else if (k == "w") {
            width = ParseNonNegativeInt(v, 0);
          }
        }
        if (colon == std::string_view::npos)
          break;
        pos = colon + 1;
      }
      buffer_.WriteScaledText(text, scale, width);
      break;
    }

    case 110:
      buffer_.SetDefaultForegroundColor(kDefaultTerminalForegroundColor);
      break;

    case 111:
      buffer_.SetDefaultBackgroundColor(kDefaultTerminalBackgroundColor);
      if (callbacks_.on_background_color_changed) {
        callbacks_.on_background_color_changed(
            buffer_.GetDefaultBackgroundColor());
      }
      break;

    case 112:
      buffer_.SetCursorColor(kDefaultTerminalCursorColor);
      break;

    case 5522: {
      size_t data_sep = rest.find(';');
      std::string_view meta =
          (data_sep == std::string_view::npos) ? rest
                                               : rest.substr(0, data_sep);
      std::string_view data = (data_sep == std::string_view::npos)
                                  ? ""
                                  : rest.substr(data_sep + 1);
      if (meta.find("type=read") != std::string_view::npos) {
        std::string clip =
            callbacks_.on_get_clipboard ? callbacks_.on_get_clipboard() : "";
        SendResponse("\x1b]5522;type=read:status=OK;" + Base64Encode(clip) +
                     "\x1b\\");
      } else if (meta.find("type=write") != std::string_view::npos &&
                 !data.empty() && callbacks_.on_set_clipboard) {
        callbacks_.on_set_clipboard(Base64DecodeString(data));
      }
      break;
    }

    case 30001:
      buffer_.PushColorStack();
      break;

    case 30101:
      buffer_.PopColorStack();
      if (callbacks_.on_background_color_changed) {
        callbacks_.on_background_color_changed(
            buffer_.GetDefaultBackgroundColor());
      }
      break;

    default:
      break;
  }
}

void EscapeParser::DispatchDcs(std::string_view payload) {
  sk_sp<SkImage> image =
      SixelDecoder::Decode(payload, buffer_.GetDefaultBackgroundColor());
  if (!image)
    return;

  float cw = std::max(1.0f, cell_width_);
  float ch = std::max(1.0f, cell_height_);
  int display_cols =
      std::max(1, static_cast<int>(std::ceil(image->width() / cw)));
  int display_rows =
      std::max(1, static_cast<int>(std::ceil(image->height() / ch)));

  while (buffer_.CursorRow() + display_rows > buffer_.Rows()) {
    buffer_.ScrollUp(1);
    if (buffer_.CursorRow() > 0)
      buffer_.MoveCursorRelative(-1, 0);
  }

  TerminalGraphicPlacement placement;
  placement.image_id = 0;
  placement.placement_id = 0;
  placement.image = image;
  placement.anchor_line_id = buffer_.CurrentCursorLineId();
  placement.anchor_col = buffer_.CursorCol();
  placement.display_cols = display_cols;
  placement.display_rows = display_rows;
  placement.z_index = -1;
  placement.is_alt_screen = buffer_.IsAlternateScreen();
  buffer_.AddGraphicPlacement(placement);

  buffer_.CarriageReturn();
  for (int r = 0; r < display_rows; ++r)
    buffer_.Index();
}

void EscapeParser::DispatchApc(std::string_view payload) {
  if (!payload.empty() && payload[0] == 'G') {
    kitty_graphics_.HandleCommand(
        payload.substr(1), buffer_, cell_width_, cell_height_,
        [this](std::string_view resp) { SendResponse(resp); });
  }
}

void EscapeParser::SendResponse(std::string_view response) const {
  if (!response.empty() && callbacks_.send_response)
    callbacks_.send_response(response);
}
