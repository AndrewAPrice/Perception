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

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "perception/ui/point.h"
#include "perception/ui/rectangle.h"

void InitializeToasts();

// Displays a toast notification with the given title and text.
void ShowToast(std::string_view title, std::string_view text);

// Draws active toast notifications onto the screen.
void DrawToasts(const ::perception::ui::Rectangle& draw_area);

// Processes mouse clicks on active toasts. Returns true if the click was
// consumed.
bool HandleToastClick(const ::perception::ui::Point& mouse_position);

// Removes expired toasts and schedules updates.
void UpdateToasts();

// Invalidates screen area occupied by toast notifications.
void InvalidateAllToastsArea();

// Returns whether a screen coordinate is over any active toast notification.
bool IsMouseOverToast(const ::perception::ui::Point& point);
