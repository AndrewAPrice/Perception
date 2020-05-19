// Copyright 2020 Google LLC
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

#include "perception/linux_syscalls/ioctl.h"

#include "bits/ioctl.h"
#include "perception/debug.h"

using perception::DebugPrinterSingleton;

namespace perception {
namespace linux_syscalls {

long ioctl(long file_descriptor, long request, long arg) {
	switch (request) {
			case TIOCGWINSZ:
			case TIOCSWINSZ:
				// Get/Set terminal window size.
			break;
			default:
				DebugPrinterSingleton << "Unhandled ioctl request " << (size_t)request << ", arg: " << (size_t)arg << "\n";
		}
	return 0;
}

}
}
