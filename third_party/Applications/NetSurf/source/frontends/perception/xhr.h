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

// Asynchronous network transport backing the JavaScript XMLHttpRequest and
// fetch() APIs. This header is consumed from C (dukky.c), so it only uses C
// types.

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// An in-flight request. Opaque to callers.
struct ns_xhr;

// The outcome of a request, valid only for the duration of the callback.
typedef struct ns_xhr_response {
  // HTTP status code, or 0 if the request failed at the network level.
  long status;
  // URL of the final response after following redirects.
  const char* url;
  // Response header lines, each terminated by CRLF, excluding the status line.
  const char* headers;
  size_t headers_length;
  // Raw response body bytes.
  const uint8_t* body;
  size_t body_length;
  // Network-level error description, or NULL on success.
  const char* error;
} ns_xhr_response;

// Invoked exactly once when a request completes, unless it was aborted.
typedef void (*ns_xhr_callback)(const ns_xhr_response* response, void* context);

// Starts an asynchronous request. `headers` is a NULL-terminated array of
// "Name: value" strings and may be NULL. `referer` may be NULL. `body` is only
// sent for methods other than GET and HEAD. Cookies are attached and stored by
// the underlying fetcher. Redirects are followed. The callback is always
// invoked asynchronously from the main loop. Returns NULL if the request could
// not be started, in which case the callback is never invoked.
struct ns_xhr* ns_xhr_start(const char* method, const char* url,
                            const char* referer, const char* const* headers,
                            const uint8_t* body, size_t body_length,
                            ns_xhr_callback callback, void* context);

// Cancels a request that has not yet invoked its callback and releases it.
void ns_xhr_abort(struct ns_xhr* xhr);

#ifdef __cplusplus
}
#endif
