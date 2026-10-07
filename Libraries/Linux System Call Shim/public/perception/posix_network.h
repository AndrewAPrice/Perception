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

#include <types.h>

#include <string_view>

namespace perception {

// Connects a TCP stream socket to `host:port` using Happy Eyeballs v2 via the
// Network Manager and wraps the connected socket in a POSIX file descriptor.
// Returns the file descriptor on success, or -1 with `errno` set on failure.
int ConnectToHostAsFileDescriptor(std::string_view host, uint16 port);

}  // namespace perception
