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

#include "perception/debug.h"
#include "perception/messages.h"
#include "perception/threads.h"

#include "stdio.h"

void main() {
	printf("Hello %s %i\n", "world", 12);

	// perception::DebugPrinterSingleton << "Hello " << "world " << (size_t)12 << '\n';
	while (true) {
		perception::Yield();
	}
}