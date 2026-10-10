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

#include <string>
#include <string_view>
#include <vector>

#include "js_engine.h"
#include "quickjs.h"

namespace module {

// Registers global `run`, `pipe` (`pipe.seq`, `pipe.merge`, `pipe.from`,
// `pipe.file`), and the `proc` namespace on the global object.
void RegisterProcModule(JSContext* ctx, JsEngine& engine);

// Launches a target application, script, file, or directory in the foreground
// with jsshell's terminal pipes attached (used by `/run`), or in the background
// with no pipes attached when `background_detached` is true (used by `/help`).
bool LaunchTargetDirect(JsEngine& engine, std::string_view target,
                        const std::vector<std::string>& args,
                        bool background_detached, std::string& error_out);

// Returns a list of installed application names discovered in `/Applications`.
std::vector<std::string> DiscoverInstalledApplications();

}  // namespace module
