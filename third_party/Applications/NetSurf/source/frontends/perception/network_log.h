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

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

struct fetch;

namespace netsurf::perception {

// Represents a single network fetch request and its lifecycle metadata.
struct NetworkLogEntry {
  uint64_t id = 0;
  std::string method;
  std::string url;
  std::string scheme;
  long http_code = 0;
  std::string status;
  std::string content_type;
  size_t bytes_received = 0;
  size_t content_length = 0;
  uint64_t start_time_ms = 0;
  uint64_t ttfb_ms = 0;
  uint64_t end_time_ms = 0;
  uint64_t duration_ms = 0;
  std::vector<std::string> request_headers;
  std::vector<std::string> response_headers;
  std::string redirect_target;
  std::string tls_info;
  bool finished = false;
  bool aborted = false;
  bool error = false;
};

// Registers the global NetSurf fetch observer to capture network activity.
void InitializeNetworkLog();

// Returns the ring buffer of recorded network log entries.
const std::deque<NetworkLogEntry>& GetNetworkLog();

// Clears all recorded network log entries.
void ClearNetworkLog();

// Enables or disables recording of new network log entries.
void SetRecordingNetworkLog(bool recording);

// Returns whether new network requests are currently being recorded.
bool IsRecordingNetworkLog();

// Associates raw HTTP request headers with an active fetch entry.
void RecordRequestHeaders(struct fetch* f,
                          const std::string& raw_request_headers);

// Associates TLS connection details with an active fetch entry.
void RecordTlsInfo(struct fetch* f, const std::string& tls_info);

// Registers a callback invoked whenever the network log changes.
int AddNetworkLogListener(std::function<void()> callback);

// Removes a previously registered network log listener by ID.
void RemoveNetworkLogListener(int id);

}  // namespace netsurf::perception

namespace network_log {

using ::netsurf::perception::AddNetworkLogListener;
using ::netsurf::perception::ClearNetworkLog;
using ::netsurf::perception::GetNetworkLog;
using ::netsurf::perception::InitializeNetworkLog;
using ::netsurf::perception::IsRecordingNetworkLog;
using ::netsurf::perception::NetworkLogEntry;
using ::netsurf::perception::RecordRequestHeaders;
using ::netsurf::perception::RecordTlsInfo;
using ::netsurf::perception::RemoveNetworkLogListener;
using ::netsurf::perception::SetRecordingNetworkLog;

}  // namespace network_log

using ::netsurf::perception::AddNetworkLogListener;
using ::netsurf::perception::ClearNetworkLog;
using ::netsurf::perception::GetNetworkLog;
using ::netsurf::perception::InitializeNetworkLog;
using ::netsurf::perception::IsRecordingNetworkLog;
using ::netsurf::perception::NetworkLogEntry;
using ::netsurf::perception::RecordRequestHeaders;
using ::netsurf::perception::RecordTlsInfo;
using ::netsurf::perception::RemoveNetworkLogListener;
using ::netsurf::perception::SetRecordingNetworkLog;
