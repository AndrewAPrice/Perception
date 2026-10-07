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

#include "clipboard.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "perception/clipboard.h"

extern "C" {
#include "netsurf/clipboard.h"
#include "utils/errors.h"
}

namespace netsurf {
namespace perception {

struct gui_clipboard_table perception_clipboard_table = {
    .get =
        [](char** buffer, size_t* length) {
          auto status_or_val = ::perception::GetClipboard();
          if (!status_or_val.Ok()) {
            *buffer = nullptr;
            *length = 0;
            return;
          }
          std::string text = status_or_val->ToString();
          if (text.empty()) {
            *buffer = nullptr;
            *length = 0;
            return;
          }
          *buffer = static_cast<char*>(malloc(text.size()));
          if (*buffer == nullptr) {
            *length = 0;
            return;
          }
          memcpy(*buffer, text.data(), text.size());
          *length = text.size();
        },
    .set =
        [](const char* buffer, size_t length, nsclipboard_styles styles[],
           int n_styles) {
          if (buffer == nullptr || length == 0) {
            ::perception::SetClipboard(std::string_view(""));
            return;
          }
          ::perception::SetClipboard(std::string_view(buffer, length));
        }};

}  // namespace perception
}  // namespace netsurf
