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

#include <cstdint>
#include <functional>

extern "C" {
#include "utils/errors.h"
#include "netsurf/download.h"
}

namespace netsurf {
namespace perception {

extern struct gui_download_table perception_download_table;

// Summary statistics for active and completed downloads.
struct DownloadSummary {
  int active_count = 0;
  int total_count = 0;
  uint64_t active_received_bytes = 0;
  uint64_t active_total_bytes = 0;
  int average_percent = 0;
};

// Opens or focuses the Downloads manager window.
void ShowDownloadsWindow();

// Returns aggregated download progress across all active and finished
// downloads.
DownloadSummary GetDownloadSummary();

// Registers a listener callback invoked whenever download state or progress
// changes. Returns a listener ID that can be passed to RemoveDownloadListener.
int AddDownloadListener(std::function<void()> on_changed);

// Unregisters a previously registered download listener by ID.
void RemoveDownloadListener(int listener_id);

}  // namespace perception
}  // namespace netsurf
