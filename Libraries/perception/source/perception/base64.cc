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

#include "perception/base64.h"

namespace perception {

namespace {

static const char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

}  // namespace

std::string Base64Encode(const uint8_t* data, size_t length) {
  std::string ret;
  ret.reserve(((length + 2) / 3) * 4);
  for (size_t i = 0; i < length; i += 3) {
    size_t rem = length - i;
    uint32_t a = data[i];
    uint32_t b = rem > 1 ? data[i + 1] : 0;
    uint32_t c = rem > 2 ? data[i + 2] : 0;
    uint32_t triple = (a << 16) | (b << 8) | c;
    ret.push_back(kBase64Chars[(triple >> 18) & 0x3F]);
    ret.push_back(kBase64Chars[(triple >> 12) & 0x3F]);
    ret.push_back(rem > 1 ? kBase64Chars[(triple >> 6) & 0x3F] : '=');
    ret.push_back(rem > 2 ? kBase64Chars[triple & 0x3F] : '=');
  }
  return ret;
}

}  // namespace perception
