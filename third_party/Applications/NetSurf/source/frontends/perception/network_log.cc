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

#include "network_log.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <string_view>
#include <unordered_map>
#include <utility>

extern "C" {
#include "content/fetch.h"
#include "utils/nsurl.h"
}

namespace netsurf::perception {

namespace {

// Maximum number of network log entries retained in the ring buffer.
constexpr size_t kMaxNetworkLogEntries = 500;

// Lowercase prefix for the HTTP Content-Type header.
constexpr std::string_view kContentTypePrefix = "content-type:";

// Lowercase prefix for the HTTP Content-Length header.
constexpr std::string_view kContentLengthPrefix = "content-length:";

// Lowercase prefix for the HTTP Authorization header.
constexpr std::string_view kAuthorizationPrefix = "authorization:";

// Lowercase prefix for the HTTP Proxy-Authorization header.
constexpr std::string_view kProxyAuthorizationPrefix = "proxy-authorization:";

// Prefix for the HTTP status line in response headers.
constexpr std::string_view kHttpStatusLinePrefix = "HTTP/";

std::deque<NetworkLogEntry> g_entries;
std::unordered_map<struct fetch*, uint64_t> g_active_fetches;
std::unordered_map<int, std::function<void()>> g_listeners;
uint64_t g_next_entry_id = 1;
int g_next_listener_id = 1;
bool g_recording = true;
bool g_initialized = false;

uint64_t NowMs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

std::string TrimWhitespace(std::string_view sv) {
  size_t start = 0;
  while (start < sv.size() &&
         std::isspace(static_cast<unsigned char>(sv[start])))
    ++start;
  size_t end = sv.size();
  while (end > start && std::isspace(static_cast<unsigned char>(sv[end - 1])))
    --end;
  return std::string(sv.substr(start, end - start));
}

bool StartsWithCaseInsensitive(std::string_view str,
                               std::string_view lower_prefix) {
  if (str.size() < lower_prefix.size())
    return false;
  for (size_t i = 0; i < lower_prefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(str[i])) != lower_prefix[i])
      return false;
  }
  return true;
}

std::string RedactHeaderLine(std::string_view line) {
  if (!StartsWithCaseInsensitive(line, kAuthorizationPrefix) &&
      !StartsWithCaseInsensitive(line, kProxyAuthorizationPrefix))
    return std::string(line);

  size_t colon = line.find(':');
  if (colon == std::string_view::npos)
    return std::string(line);

  std::string_view name = line.substr(0, colon + 1);
  std::string value = TrimWhitespace(line.substr(colon + 1));
  size_t space = value.find(' ');
  if (space != std::string::npos)
    return std::string(name) + " " + value.substr(0, space) + " [REDACTED]";
  return std::string(name) + " [REDACTED]";
}

NetworkLogEntry* FindEntry(struct fetch* f) {
  if (f == nullptr)
    return nullptr;
  auto it = g_active_fetches.find(f);
  if (it == g_active_fetches.end())
    return nullptr;
  uint64_t target_id = it->second;
  for (auto rit = g_entries.rbegin(); rit != g_entries.rend(); ++rit) {
    if (rit->id == target_id)
      return &(*rit);
  }
  return nullptr;
}

void NotifyListeners() {
  auto listeners_copy = g_listeners;
  for (const auto& [id, callback] : listeners_copy) {
    if (callback)
      callback();
  }
}

void OnFetchStarted(struct fetch* f, struct nsurl* url, bool is_post) {
  if (!g_recording || f == nullptr)
    return;

  if (g_entries.size() >= kMaxNetworkLogEntries)
    g_entries.pop_front();

  NetworkLogEntry entry;
  entry.id = g_next_entry_id++;
  entry.method = is_post ? "POST" : "GET";
  if (url != nullptr && nsurl_access(url) != nullptr)
    entry.url = nsurl_access(url);

  if (url != nullptr) {
    lwc_string* scheme = nsurl_get_component(url, NSURL_SCHEME);
    if (scheme != nullptr) {
      entry.scheme.assign(lwc_string_data(scheme), lwc_string_length(scheme));
      lwc_string_unref(scheme);
    }
  }

  entry.status = "Pending";
  entry.start_time_ms = NowMs();

  g_active_fetches[f] = entry.id;
  g_entries.push_back(std::move(entry));
  NotifyListeners();
}

void OnFetchHttpCode(struct fetch* f, long code) {
  NetworkLogEntry* entry = FindEntry(f);
  if (entry == nullptr)
    return;

  entry->http_code = code;
  if (entry->status == "Pending" && code > 0)
    entry->status = std::to_string(code);
  NotifyListeners();
}

void OnFetchMessage(struct fetch* f, const fetch_msg* msg) {
  NetworkLogEntry* entry = FindEntry(f);
  if (entry == nullptr || msg == nullptr)
    return;

  uint64_t now_ms = NowMs();
  entry->duration_ms =
      (now_ms >= entry->start_time_ms) ? (now_ms - entry->start_time_ms) : 0;

  switch (msg->type) {
    case FETCH_HEADER: {
      if (entry->ttfb_ms == 0)
        entry->ttfb_ms = entry->duration_ms;

      const char* buf =
          reinterpret_cast<const char*>(msg->data.header_or_data.buf);
      size_t len = msg->data.header_or_data.len;
      if (buf != nullptr && len > 0) {
        std::string line = TrimWhitespace(std::string_view(buf, len));
        if (!line.empty()) {
          if (entry->response_headers.empty() &&
              line.rfind(kHttpStatusLinePrefix, 0) == 0) {
            size_t space = line.find(' ');
            if (space != std::string::npos) {
              std::string status_text = TrimWhitespace(line.substr(space + 1));
              if (!status_text.empty())
                entry->status = status_text;
            }
          } else {
            entry->response_headers.push_back(line);
            if (StartsWithCaseInsensitive(line, kContentTypePrefix)) {
              entry->content_type = TrimWhitespace(
                  std::string_view(line).substr(kContentTypePrefix.size()));
            } else if (StartsWithCaseInsensitive(line, kContentLengthPrefix)) {
              std::string len_str = TrimWhitespace(
                  std::string_view(line).substr(kContentLengthPrefix.size()));
              entry->content_length =
                  static_cast<size_t>(std::strtoull(len_str.c_str(), nullptr, 10));
            }
          }
        }
      }
      break;
    }
    case FETCH_DATA:
      if (entry->ttfb_ms == 0)
        entry->ttfb_ms = entry->duration_ms;
      entry->bytes_received += msg->data.header_or_data.len;
      break;

    case FETCH_FINISHED:
      entry->finished = true;
      entry->end_time_ms = now_ms;
      if (entry->status == "Pending") {
        if (entry->http_code > 0)
          entry->status = std::to_string(entry->http_code) + " OK";
        else
          entry->status = "200 OK";
      }
      break;

    case FETCH_REDIRECT:
      entry->redirect_target =
          (msg->data.redirect != nullptr) ? msg->data.redirect : "";
      if (entry->http_code > 0) {
        entry->status = std::to_string(entry->http_code) + " Redirect -> " +
                        entry->redirect_target;
      } else {
        entry->status = "Redirect -> " + entry->redirect_target;
      }
      entry->finished = true;
      entry->end_time_ms = now_ms;
      break;

    case FETCH_ERROR:
      entry->error = true;
      entry->finished = true;
      entry->end_time_ms = now_ms;
      entry->status = std::string("Error: ") +
                      (msg->data.error != nullptr ? msg->data.error : "Unknown");
      break;

    case FETCH_CERT_ERR:
      entry->error = true;
      entry->finished = true;
      entry->end_time_ms = now_ms;
      entry->status = "Certificate Error";
      break;

    case FETCH_NOTMODIFIED:
      entry->http_code = 304;
      entry->status = "304 Not Modified";
      entry->finished = true;
      entry->end_time_ms = now_ms;
      break;

    case FETCH_AUTH:
      entry->status = "401 Auth Required";
      break;

    case FETCH_TIMEDOUT:
      entry->error = true;
      entry->status = "Timed Out";
      break;

    default:
      break;
  }

  NotifyListeners();
}

void OnFetchFinished(struct fetch* f) {
  NetworkLogEntry* entry = FindEntry(f);
  if (entry == nullptr)
    return;

  uint64_t now_ms = NowMs();
  entry->duration_ms =
      (now_ms >= entry->start_time_ms) ? (now_ms - entry->start_time_ms) : 0;
  entry->end_time_ms = now_ms;
  if (!entry->finished) {
    entry->aborted = true;
    entry->finished = true;
    if (entry->status == "Pending")
      entry->status = "Aborted";
  }

  g_active_fetches.erase(f);
  NotifyListeners();
}

const struct fetch_observer kFetchObserver = {
    .started = OnFetchStarted,
    .message = OnFetchMessage,
    .http_code = OnFetchHttpCode,
    .finished = OnFetchFinished,
};

}  // namespace

void InitializeNetworkLog() {
  if (g_initialized)
    return;
  g_initialized = true;
  fetch_set_observer(&kFetchObserver);
}

const std::deque<NetworkLogEntry>& GetNetworkLog() { return g_entries; }

void ClearNetworkLog() {
  g_entries.clear();
  g_active_fetches.clear();
  NotifyListeners();
}

void SetRecordingNetworkLog(bool recording) { g_recording = recording; }

bool IsRecordingNetworkLog() { return g_recording; }

void RecordRequestHeaders(struct fetch* f,
                          const std::string& raw_request_headers) {
  NetworkLogEntry* entry = FindEntry(f);
  if (entry == nullptr)
    return;

  entry->request_headers.clear();
  size_t pos = 0;
  bool first_line = true;
  while (pos < raw_request_headers.size()) {
    size_t line_end = raw_request_headers.find('\n', pos);
    if (line_end == std::string::npos)
      line_end = raw_request_headers.size();

    std::string line = TrimWhitespace(
        std::string_view(raw_request_headers).substr(pos, line_end - pos));
    pos = line_end + 1;

    if (line.empty())
      break;

    if (first_line) {
      first_line = false;
      size_t space = line.find(' ');
      if (space != std::string::npos)
        entry->method = line.substr(0, space);
      entry->request_headers.push_back(line);
    } else {
      entry->request_headers.push_back(RedactHeaderLine(line));
    }
  }

  NotifyListeners();
}

void RecordTlsInfo(struct fetch* f, const std::string& tls_info) {
  NetworkLogEntry* entry = FindEntry(f);
  if (entry == nullptr)
    return;

  entry->tls_info = tls_info;
  NotifyListeners();
}

int AddNetworkLogListener(std::function<void()> callback) {
  int id = g_next_listener_id++;
  g_listeners[id] = std::move(callback);
  return id;
}

void RemoveNetworkLogListener(int id) { g_listeners.erase(id); }

}  // namespace netsurf::perception
