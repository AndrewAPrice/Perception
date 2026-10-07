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

namespace output {

// Overrides the screen to solid blue and enables printing characters directly
// to the framebuffer.
void EnableBlueScreen();

// Prints a single character in white text onto the Blue Screen if enabled.
void PrintBlueScreenCharacter(char c);

// Resets the Blue Screen state for unit tests.
void DisableBlueScreenForTest();

}  // namespace output
