// Copyright 2021 Google LLC
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

#include <chrono>
#include <functional>

namespace perception {

// Returns the time since the kernel started.
std::chrono::microseconds GetTimeSinceKernelStarted();

// Sleeps the current fiber and returns after the duration has passed.
void SleepForDuration(std::chrono::microseconds time);

// Sleeps the current fiber and returns after the duration since the
// kernel started has passed.
void SleepUntilTimeSinceKernelStarted(std::chrono::microseconds time);

// Calls the on_duration function after a duration has passed.
void AfterDuration(std::chrono::microseconds time,
	std::function<void()> on_duration);

// Calls the at_time function after the duration since the kernel
// started has passed.
void AfterTimeSinceKernelStarted(std::chrono::microseconds time,
	std::function<void()> at_time);

}
