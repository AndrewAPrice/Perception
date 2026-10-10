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

#include <string_view>

#include "js_engine.h"
#include "quickjs.h"

// Opens the full-screen interactive Object / JSON Inspector on the alternate
// screen buffer (`\x1b[?1049h`) for inspecting a live JSValue.
void RunObjectInspector(JsEngine& engine, JSValue root_value,
                        std::string_view title);

// Opens the full-screen interactive Memory Explorer on the alternate screen
// buffer (`\x1b[?1049h`), displaying QuickJS & Perception OS memory metrics
// and all live global variables, history responses, and namespaces.
void RunMemoryExplorer(JsEngine& engine);
