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

void main() {
	perception::DebugPrinterSingleton << "1\n";

	while (true) {
		for (int i = 0; i < 3; i++) {
			for (int j = 0; j < 4; j++) {
				size_t status = (size_t)perception::SendMessage(1L, 99L, 1L, 2L, 3L, 4L, 5L);
				perception::DebugPrinterSingleton << "Status sending to 1 was " << status << "\n";
			}
			for (int j = 0; j < 4; j++) {
				size_t status = (size_t)perception::SendMessage(2L, 99L, 1L, 2L, 3L, 4L, 5L);
				perception::DebugPrinterSingleton << "Status sending to 2 was " << status << "\n";
			}
			if (i == 2) {
				for (int j = 0; j < 4; j++) {
					size_t status = (size_t)perception::SendMessage(3L, 99L, 1L, 2L, 3L, 4L, 5L);
					perception::DebugPrinterSingleton << "Status sending to 3 was " << status << "\n";
				}
			}
			perception::Yield();
		}
	}
}