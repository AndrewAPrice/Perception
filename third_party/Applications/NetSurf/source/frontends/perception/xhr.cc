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

#include "xhr.h"

#include <stdlib.h>
#include <strings.h>

#include <string>
#include <string_view>
#include <vector>

#include "misc.h"

extern "C" {
#include "content/fetch.h"
#include "utils/log.h"
#include "utils/nsurl.h"
}

namespace {

// Maximum number of redirects followed before a request fails.
constexpr int kMaxRedirects = 10;

// Prefix identifying a status line forwarded alongside the header lines.
constexpr std::string_view kStatusLinePrefix = "HTTP/";

// Accept header sent when the script does not supply one, matching browsers.
constexpr char kDefaultAcceptHeader[] = "Accept: */*";

// Error reported when the redirect limit is exceeded.
constexpr char kTooManyRedirectsError[] = "TooManyRedirects";

// Error reported when a redirect target cannot be parsed or fetched.
constexpr char kBadRedirectError[] = "BadRedirect";

// Error reported when the fetcher fails without providing a reason.
constexpr char kUnknownFetchError[] = "FetchFailed";

// HTTP status codes after which a redirect is re-issued as a GET request.
constexpr long kHttpMovedPermanently = 301;
constexpr long kHttpFound = 302;
constexpr long kHttpSeeOther = 303;

// HTTP status reported for a FETCH_NOTMODIFIED response.
constexpr long kHttpNotModified = 304;

bool StartsWithIgnoringCase(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() &&
         strncasecmp(text.data(), prefix.data(), prefix.size()) == 0;
}

}  // namespace

struct ns_xhr {
  std::string method;
  nsurl* url = nullptr;
  nsurl* referer = nullptr;
  std::vector<std::string> headers;
  std::string body;
  ns_xhr_callback callback = nullptr;
  void* context = nullptr;

  struct fetch* fetch = nullptr;
  int redirects = 0;
  long status = 0;
  std::string response_headers;
  std::string response_body;
  std::string error;
  bool delivery_scheduled = false;
};

namespace {

void IgnoreFetchMessage(const fetch_msg* msg, void* p) {}

void ReleaseXhr(ns_xhr* xhr) {
  if (xhr->url != nullptr)
    nsurl_unref(xhr->url);
  if (xhr->referer != nullptr)
    nsurl_unref(xhr->referer);
  delete xhr;
}

void DeliverResponse(void* p) {
  auto* xhr = static_cast<ns_xhr*>(p);
  xhr->delivery_scheduled = false;

  ns_xhr_response response;
  response.status = xhr->status;
  response.url = nsurl_access(xhr->url);
  response.headers = xhr->response_headers.c_str();
  response.headers_length = xhr->response_headers.size();
  response.body =
      reinterpret_cast<const uint8_t*>(xhr->response_body.data());
  response.body_length = xhr->response_body.size();
  response.error = xhr->error.empty() ? nullptr : xhr->error.c_str();
  xhr->callback(&response, xhr->context);
  ReleaseXhr(xhr);
}

void Complete(ns_xhr& xhr) {
  xhr.fetch = nullptr;
  xhr.delivery_scheduled = true;
  ::netsurf::perception::perception_misc_table.schedule(0, DeliverResponse,
                                                        &xhr);
}

// Methods whose requests never carry a body.
bool MethodHasNoBody(const std::string& method) {
  return strcasecmp(method.c_str(), "GET") == 0 ||
         strcasecmp(method.c_str(), "HEAD") == 0;
}

// Builds the header list for the next fetch attempt, adding Referer, Origin,
// and an explicit Content-Length so the fetcher treats the body as binary.
std::vector<std::string> BuildRequestHeaders(const ns_xhr& xhr) {
  std::vector<std::string> headers;
  bool has_referer = false;
  bool has_accept = false;
  for (const std::string& header : xhr.headers) {
    if (StartsWithIgnoringCase(header, "Content-Length:"))
      continue;
    if (MethodHasNoBody(xhr.method) &&
        StartsWithIgnoringCase(header, "Content-Type:"))
      continue;
    if (StartsWithIgnoringCase(header, "Referer:"))
      has_referer = true;
    if (StartsWithIgnoringCase(header, "Accept:"))
      has_accept = true;
    headers.push_back(header);
  }

  if (!has_accept)
    headers.push_back(kDefaultAcceptHeader);

  if (xhr.referer != nullptr) {
    if (!has_referer)
      headers.push_back(std::string("Referer: ") + nsurl_access(xhr.referer));

    if (!MethodHasNoBody(xhr.method)) {
      char* origin = nullptr;
      size_t origin_length = 0;
      if (nsurl_get(xhr.referer,
                    static_cast<nsurl_component>(NSURL_SCHEME | NSURL_HOST |
                                                 NSURL_PORT),
                    &origin, &origin_length) == NSERROR_OK) {
        std::string origin_value(origin, origin_length);
        // nsurl_get appends the path separator, which Origin must not have.
        while (!origin_value.empty() && origin_value.back() == '/')
          origin_value.pop_back();
        headers.push_back("Origin: " + origin_value);
        free(origin);
      }
    }
  }

  if (!MethodHasNoBody(xhr.method))
    headers.push_back("Content-Length: " + std::to_string(xhr.body.size()));
  return headers;
}

void HandleFetchMessage(const fetch_msg* msg, void* p);

bool StartFetch(ns_xhr& xhr) {
  std::vector<std::string> headers = BuildRequestHeaders(xhr);
  std::vector<const char*> header_pointers;
  header_pointers.reserve(headers.size() + 1);
  for (const std::string& header : headers)
    header_pointers.push_back(header.c_str());
  header_pointers.push_back(nullptr);

  // The fetcher only distinguishes GET from POST, keyed on the presence of a
  // body pointer, so every method with a body is sent as POST.
  const char* post = MethodHasNoBody(xhr.method) ? nullptr : xhr.body.c_str();

  xhr.response_headers.clear();
  xhr.response_body.clear();
  nserror error = fetch_start(xhr.url, xhr.referer, HandleFetchMessage, &xhr,
                              /*only_2xx=*/false, post,
                              /*post_multipart=*/nullptr,
                              /*verifiable=*/true, /*downgrade_tls=*/false,
                              header_pointers.data(), &xhr.fetch);
  if (error != NSERROR_OK) {
    NSLOG(netsurf, INFO, "XHR fetch_start failed for %s: %d",
          nsurl_access(xhr.url), error);
    xhr.fetch = nullptr;
    return false;
  }
  return true;
}

void FollowRedirect(ns_xhr& xhr, const char* location) {
  xhr.fetch = nullptr;
  if (++xhr.redirects > kMaxRedirects) {
    xhr.error = kTooManyRedirectsError;
    Complete(xhr);
    return;
  }

  nsurl* target = nullptr;
  if (location == nullptr ||
      nsurl_join(xhr.url, location, &target) != NSERROR_OK) {
    xhr.error = kBadRedirectError;
    Complete(xhr);
    return;
  }
  nsurl_unref(xhr.url);
  xhr.url = target;

  if (xhr.status == kHttpMovedPermanently || xhr.status == kHttpFound ||
      xhr.status == kHttpSeeOther) {
    if (!MethodHasNoBody(xhr.method)) {
      xhr.method = "GET";
      xhr.body.clear();
    }
  }

  if (!StartFetch(xhr)) {
    xhr.error = kBadRedirectError;
    Complete(xhr);
  }
}

void HandleFetchMessage(const fetch_msg* msg, void* p) {
  auto* xhr = static_cast<ns_xhr*>(p);
  switch (msg->type) {
    case FETCH_HEADER: {
      std::string_view line(
          reinterpret_cast<const char*>(msg->data.header_or_data.buf),
          msg->data.header_or_data.len);
      if (!StartsWithIgnoringCase(line, kStatusLinePrefix))
        xhr->response_headers.append(line);
      break;
    }
    case FETCH_DATA:
      xhr->response_body.append(
          reinterpret_cast<const char*>(msg->data.header_or_data.buf),
          msg->data.header_or_data.len);
      break;
    case FETCH_FINISHED:
      xhr->status = fetch_http_code(xhr->fetch);
      Complete(*xhr);
      break;
    case FETCH_NOTMODIFIED:
      xhr->status = kHttpNotModified;
      Complete(*xhr);
      break;
    case FETCH_REDIRECT:
      xhr->status = fetch_http_code(xhr->fetch);
      FollowRedirect(*xhr, msg->data.redirect);
      break;
    case FETCH_ERROR:
    case FETCH_TIMEDOUT:
    case FETCH_CERT_ERR:
    case FETCH_SSL_ERR:
    case FETCH_AUTH:
      xhr->status = 0;
      xhr->error = (msg->type == FETCH_ERROR && msg->data.error != nullptr)
                       ? msg->data.error
                       : kUnknownFetchError;
      Complete(*xhr);
      break;
    default:
      break;
  }
}

}  // namespace

extern "C" struct ns_xhr* ns_xhr_start(const char* method, const char* url,
                                       const char* referer,
                                       const char* const* headers,
                                       const uint8_t* body, size_t body_length,
                                       ns_xhr_callback callback,
                                       void* context) {
  if (method == nullptr || url == nullptr || callback == nullptr)
    return nullptr;

  auto* xhr = new ns_xhr();
  xhr->method = method;
  xhr->callback = callback;
  xhr->context = context;
  if (body != nullptr)
    xhr->body.assign(reinterpret_cast<const char*>(body), body_length);
  if (headers != nullptr) {
    for (size_t i = 0; headers[i] != nullptr; ++i)
      xhr->headers.push_back(headers[i]);
  }

  if (nsurl_create(url, &xhr->url) != NSERROR_OK) {
    ReleaseXhr(xhr);
    return nullptr;
  }
  if (referer != nullptr && nsurl_create(referer, &xhr->referer) != NSERROR_OK)
    xhr->referer = nullptr;

  if (!StartFetch(*xhr)) {
    ReleaseXhr(xhr);
    return nullptr;
  }
  return xhr;
}

extern "C" void ns_xhr_abort(struct ns_xhr* xhr) {
  if (xhr == nullptr)
    return;
  if (xhr->fetch != nullptr) {
    fetch_change_callback(xhr->fetch, IgnoreFetchMessage, nullptr);
    fetch_abort(xhr->fetch);
    xhr->fetch = nullptr;
  }
  if (xhr->delivery_scheduled)
    ::netsurf::perception::perception_misc_table.schedule(-1, DeliverResponse,
                                                          xhr);
  ReleaseXhr(xhr);
}
