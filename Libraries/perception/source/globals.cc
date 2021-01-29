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

#include "types.h"

namespace {
/*
// A function to call upon exiting the program.
struct ExitFunction {
	void (*function)(void *);
	void *object;
	struct ExitFunction* next;
};

// Linked list of exit functions.
ExitFunction* last_exit_function = nullptr;
*/
}

extern "C" {
	// Used by the compiler, but what for?
	void *__dso_handle = 0;
}
/*
// Registers an exit function. This is a function GCC expects to exist.
extern "C" int __cxa_atexit(void (function)(void *), void *object, void *dso) {
	// We don't yet support dynamic shared objects, so we can ignore the dso argument.

	// Add a new item to the front of the linked list.
	ExitFunction* exit_function = new ExitFunction();
	exit_function->function = function;
	exit_function->object = object;
	exit_function->next = last_exit_function;
	last_exit_function = exit_function;
	return 0;
}

// Calls the exit functions.
extern "C" void __cxa_finalize(void *function) {
	if (function != nullptr) {
		// Call exit functions that match the provided function.
		ExitFunction* previous = nullptr;
		ExitFunction* this_func = last_exit_function;

		while (this_func != nullptr) {
			if (this_func->function == function) {
				// Found an exit function that matches what we want to call.
				this_func->function(this_func->object);

				// Remove us from the linked list and iterate to the next exit function.
				ExitFunction* next = this_func->next;
				delete this_func;

				if (previous != nullptr) {
					previous->next = next;
				} else {
					last_exit_function = next;
				}

				this_func = next;
			} else {
				// Skip over this exit function.
				previous = this_func;
				this_func = this_func->next;
			}
		}
	} else {
		// Call all exit functions.
		while (last_exit_function != nullptr) {
			// Call the exit function.
			last_exit_function->function(last_exit_function->object);

			// Remove this function from our linked list.
			ExitFunction* next = last_exit_function->next;
			delete last_exit_function;
			last_exit_function = next;
		}

	}
}*/
