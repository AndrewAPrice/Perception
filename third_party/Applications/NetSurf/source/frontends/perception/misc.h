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

extern "C" {
#include <stddef.h>
#include <stdint.h>
#include "utils/errors.h"
#include "netsurf/misc.h"
}

namespace netsurf {
namespace perception {

const char* GetInitialUrl();
void SetInitialUrl(const char* url);

void gui_quit();

extern struct gui_misc_table perception_misc_table;

}  // namespace perception
}  // namespace netsurf
