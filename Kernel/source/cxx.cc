// Copyright 2024 Google LLC
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

// Methods used implicitly by C++.

// Registers a destructor to be called on exit.
extern "C" int __cxa_atexit(void (*destructor)(void *), void *arg,
                            void *__dso_handle) {
  // The kernel never exits like a normal program (the computer powers off), so
  // there's nothing to do here.
}
