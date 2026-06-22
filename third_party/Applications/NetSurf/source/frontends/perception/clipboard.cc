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

extern "C" {
#include "utils/errors.h"
#include "netsurf/clipboard.h"
}

namespace netsurf {
namespace perception {

struct gui_clipboard_table perception_clipboard_table = {
    .get =
        [](char** buffer, size_t* length) {
          *buffer = NULL;
          *length = 0;
        },
    .set = [](const char* buffer, size_t length, nsclipboard_styles styles[],
              int n_styles) {}};

}  // namespace perception
}  // namespace netsurf
