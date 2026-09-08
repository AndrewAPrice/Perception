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

#include "http.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "bearssl.h"
#include "perception/fibers.h"
#include "perception/network/network_service.h"
#include "perception/processes.h"
#include "perception/random.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/time.h"
#include "utils/errors.h"

extern "C" {
#include "content/fetch.h"
#include "content/fetchers.h"
#include "netsurf/fetch.h"
#include "utils/log.h"
#include "utils/nsurl.h"
}

namespace netsurf {
namespace perception {

struct br_x509_unsafe_context {
  const br_x509_class* vtable;
  br_x509_minimal_context minimal;
};

static void unsafe_start_chain(const br_x509_class** ctx,
                               const char* server_name) {
  br_x509_unsafe_context* uc = (br_x509_unsafe_context*)ctx;
  uc->minimal.vtable->start_chain(&uc->minimal.vtable, nullptr);
}
static void unsafe_start_cert(const br_x509_class** ctx, uint32_t length) {
  br_x509_unsafe_context* uc = (br_x509_unsafe_context*)ctx;
  uc->minimal.vtable->start_cert(&uc->minimal.vtable, length);
}
static void unsafe_append(const br_x509_class** ctx, const unsigned char* buf,
                          size_t len) {
  br_x509_unsafe_context* uc = (br_x509_unsafe_context*)ctx;
  uc->minimal.vtable->append(&uc->minimal.vtable, buf, len);
}
static void unsafe_end_cert(const br_x509_class** ctx) {
  br_x509_unsafe_context* uc = (br_x509_unsafe_context*)ctx;
  uc->minimal.vtable->end_cert(&uc->minimal.vtable);
}
static unsigned unsafe_end_chain(const br_x509_class** ctx) {
  br_x509_unsafe_context* uc = (br_x509_unsafe_context*)ctx;
  uc->minimal.vtable->end_chain(&uc->minimal.vtable);
  return 0;  // Ignore all verification errors
}
static const br_x509_pkey* unsafe_get_pkey(const br_x509_class* const* ctx,
                                           unsigned* usages) {
  br_x509_unsafe_context* uc = (br_x509_unsafe_context*)ctx;
  if (usages != nullptr) {
    *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
  }
  return &uc->minimal.pkey;
}

static const br_x509_class br_x509_unsafe_vtable = {
    sizeof(br_x509_unsafe_context),
    unsafe_start_chain,
    unsafe_start_cert,
    unsafe_append,
    unsafe_end_cert,
    unsafe_end_chain,
    unsafe_get_pkey};

struct http_fetch_context {
  struct fetch* parent_fetch;
  std::string host;
  int port;
  std::string path_and_query;
  int sock;
  bool active;
  bool finished;
  std::string request_data;
  size_t sent_bytes;
  std::string response_data;
  bool failed;
  std::string failure_reason;
  bool in_poll;
  bool freed;

  // HTTPS Support
  bool is_https;
  bool socket_closed;
  br_ssl_client_context sc;
  br_x509_unsafe_context uc;
  unsigned char io_buffer[BR_SSL_BUFSIZE_BIDI];
};

static std::mutex active_fetches_mutex;
static std::vector<http_fetch_context*> active_http_fetches;

static bool http_fetch_initialise(lwc_string* scheme) { return true; }

static void http_fetch_finalise(lwc_string* scheme) {}

static bool http_fetch_acceptable(const struct nsurl* url) { return true; }

static void* http_fetch_setup(struct fetch* parent_fetch, struct nsurl* url,
                              bool only_2xx, bool downgrade_tls,
                              const char* post_urlenc,
                              const struct fetch_multipart_data* post_multipart,
                              const char** headers) {
  auto ctx = new http_fetch_context();
  ctx->parent_fetch = parent_fetch;
  ctx->sock = -1;
  ctx->active = false;
  ctx->finished = false;
  ctx->sent_bytes = 0;
  ctx->failed = false;
  ctx->failure_reason = "";
  ctx->socket_closed = false;
  ctx->in_poll = false;
  ctx->freed = false;

  ctx->is_https = false;
  lwc_string* scheme_lwc = nsurl_get_component(url, NSURL_SCHEME);
  if (scheme_lwc) {
    std::string scheme_str = lwc_string_data(scheme_lwc);
    if (scheme_str == "https") {
      ctx->is_https = true;
    }
    lwc_string_unref(scheme_lwc);
  }

  lwc_string* host_lwc = nsurl_get_component(url, NSURL_HOST);
  if (host_lwc) {
    ctx->host = lwc_string_data(host_lwc);
    lwc_string_unref(host_lwc);
  } else {
    ctx->host = "localhost";
  }

  lwc_string* port_lwc = nsurl_get_component(url, NSURL_PORT);
  if (port_lwc) {
    ctx->port = std::atoi(lwc_string_data(port_lwc));
    lwc_string_unref(port_lwc);
  } else {
    ctx->port = ctx->is_https ? 443 : 80;
  }

  lwc_string* path_lwc = nsurl_get_component(url, NSURL_PATH);
  if (path_lwc) {
    ctx->path_and_query = lwc_string_data(path_lwc);
    lwc_string_unref(path_lwc);
  } else {
    ctx->path_and_query = "/";
  }

  lwc_string* query_lwc = nsurl_get_component(url, NSURL_QUERY);
  if (query_lwc) {
    ctx->path_and_query += "?" + std::string(lwc_string_data(query_lwc));
    lwc_string_unref(query_lwc);
  }

  std::string method = post_urlenc ? "POST" : "GET";
  ctx->request_data = method + " " + ctx->path_and_query + " HTTP/1.1\r\n";
  ctx->request_data += "Host: " + ctx->host + "\r\n";

  bool has_user_agent = false;
  bool has_accept = false;
  bool has_connection = false;
  bool has_content_type = false;
  bool has_content_length = false;

  if (headers) {
    for (int i = 0; headers[i] != nullptr; ++i) {
      std::string h(headers[i]);
      if (h.rfind("User-Agent:", 0) == 0 || h.rfind("user-agent:", 0) == 0)
        has_user_agent = true;
      if (h.rfind("Accept:", 0) == 0 || h.rfind("accept:", 0) == 0)
        has_accept = true;
      if (h.rfind("Connection:", 0) == 0 || h.rfind("connection:", 0) == 0)
        has_connection = true;
      if (h.rfind("Content-Type:", 0) == 0 || h.rfind("content-type:", 0) == 0)
        has_content_type = true;
      if (h.rfind("Content-Length:", 0) == 0 ||
          h.rfind("content-length:", 0) == 0)
        has_content_length = true;
      ctx->request_data += h + "\r\n";
    }
  }

  if (!has_user_agent) {
    ctx->request_data += "User-Agent: NetSurf/3.11 (Perception; x86_64)\r\n";
  }
  if (!has_accept) {
    ctx->request_data +=
        "Accept: "
        "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
        "Accept-Language: en-US,en;q=0.5\r\n";
  }
  if (!has_connection) {
    ctx->request_data += "Connection: close\r\n";
  }

  if (post_urlenc) {
    size_t post_len = strlen(post_urlenc);
    if (!has_content_type) {
      ctx->request_data +=
          "Content-Type: application/x-www-form-urlencoded\r\n";
    }
    if (!has_content_length) {
      ctx->request_data +=
          "Content-Length: " + std::to_string(post_len) + "\r\n";
    }
    ctx->request_data += "\r\n";
    ctx->request_data += post_urlenc;
  } else {
    ctx->request_data += "\r\n";
  }

  if (ctx->is_https) {
    // Initialize X509 unsafe engine
    ctx->uc.vtable = &br_x509_unsafe_vtable;

    // Initialize SSL client context
    br_ssl_client_init_full(&ctx->sc, &ctx->uc.minimal, nullptr, 0);

    // Set time callback to return 0 so certificate validity dates always pass
    // and certificate parsing succeeds in extracting public keys.
    br_x509_minimal_set_time_callback(
        &ctx->uc.minimal, nullptr,
        [](void*, uint32_t, uint32_t, uint32_t, uint32_t) -> int { return 0; });

    // Set unsafe X509 engine
    br_ssl_engine_set_x509(&ctx->sc.eng, &ctx->uc.vtable);

    // Inject secure entropy BEFORE reset, because br_ssl_client_reset
    // initializes the PRNG
    unsigned char entropy[32];
    for (int i = 0; i < 32; i += sizeof(size_t)) {
      size_t r = ::perception::RandomNumber();
      std::memcpy(entropy + i, &r, std::min((size_t)32 - i, sizeof(r)));
    }
    br_ssl_engine_inject_entropy(&ctx->sc.eng, entropy, 32);

    // Set buffer to bidirectional
    br_ssl_engine_set_buffer(&ctx->sc.eng, ctx->io_buffer,
                             sizeof(ctx->io_buffer), 1);

    // Reset/ready handshake
    br_ssl_client_reset(&ctx->sc, ctx->host.c_str(), 0);
  }

  return ctx;
}

static bool http_fetch_start(void* handle) {
  auto ctx = (http_fetch_context*)handle;

  ::perception::network::ResolveHostRequest resolve_req;
  resolve_req.host = ctx->host;

  auto resolve_res =
      ::perception::GetService<::perception::network::NetworkService>()
          .ResolveHost(resolve_req);
  if (!resolve_res.Ok() || resolve_res->addresses.empty()) {
    std::cout << "NetSurf HTTP Fetcher: Failed to resolve host: "
              << ctx->host.c_str() << std::endl;
    ctx->failed = true;
    ctx->failure_reason = "DnsResolutionFailed";
    ctx->active = true;
    {
      std::scoped_lock lock(active_fetches_mutex);
      active_http_fetches.push_back(ctx);
    }
    return true;
  }

  auto ip_addr = resolve_res->addresses[0];

  ctx->sock = socket(AF_INET, SOCK_STREAM, 0);
  if (ctx->sock < 0) {
    std::cout << "NetSurf HTTP Fetcher: Failed to create socket!" << std::endl;
    ctx->failed = true;
    ctx->failure_reason = "SocketCreateFailed";
    ctx->active = true;
    {
      std::scoped_lock lock(active_fetches_mutex);
      active_http_fetches.push_back(ctx);
    }
    return true;
  }

  struct sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(ctx->port);
  std::memcpy(&addr.sin_addr.s_addr, ip_addr.address, 4);

  if (connect(ctx->sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    std::cout << "NetSurf HTTP Fetcher: Failed to connect to "
              << ctx->host.c_str() << ":" << (int64)ctx->port << std::endl;
    close(ctx->sock);
    ctx->sock = -1;
    ctx->failed = true;
    ctx->failure_reason = "SocketConnectFailed";
    ctx->active = true;
    {
      std::scoped_lock lock(active_fetches_mutex);
      active_http_fetches.push_back(ctx);
    }
    return true;
  }

  int flags = fcntl(ctx->sock, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(ctx->sock, F_SETFL, flags | O_NONBLOCK);
  }
  ctx->active = true;
  {
    std::scoped_lock lock(active_fetches_mutex);
    active_http_fetches.push_back(ctx);
  }
  return true;
}

static void http_fetch_abort(void* handle) {
  auto ctx = (http_fetch_context*)handle;
  ctx->active = false;
  if (ctx->sock >= 0) {
    close(ctx->sock);
    ctx->sock = -1;
  }
}

static void http_fetch_free(void* handle) {
  auto ctx = (http_fetch_context*)handle;
  {
    std::scoped_lock lock(active_fetches_mutex);
    auto it =
        std::find(active_http_fetches.begin(), active_http_fetches.end(), ctx);
    if (it != active_http_fetches.end()) {
      active_http_fetches.erase(it);
    }
  }
  if (ctx->sock >= 0) {
    close(ctx->sock);
    ctx->sock = -1;
  }
  ctx->freed = true;
  if (!ctx->in_poll) {
    delete ctx;
  }
}

static size_t ParseHex(const std::string& s) {
  size_t val = 0;
  for (char c : s) {
    if (c >= '0' && c <= '9') {
      val = val * 16 + (c - '0');
    } else if (c >= 'a' && c <= 'f') {
      val = val * 16 + (10 + (c - 'a'));
    } else if (c >= 'A' && c <= 'F') {
      val = val * 16 + (10 + (c - 'A'));
    } else {
      break;
    }
  }
  return val;
}

static bool is_http_response_complete(const http_fetch_context* ctx) {
  if (ctx->socket_closed) return true;

  const std::string& resp = ctx->response_data;
  size_t ddelim = resp.find("\r\n\r\n");
  size_t delim_len = 4;
  if (ddelim == std::string::npos) {
    ddelim = resp.find("\n\n");
    delim_len = 2;
  }
  if (ddelim == std::string::npos) {
    return false;  // Headers not fully received yet
  }

  std::string headers = resp.substr(0, ddelim);
  std::string body = resp.substr(ddelim + delim_len);

  // Parse Status Code
  long status_code = 200;
  size_t first_line_end = headers.find("\r\n");
  if (first_line_end == std::string::npos) first_line_end = headers.find("\n");
  if (first_line_end != std::string::npos) {
    std::string first_line = headers.substr(0, first_line_end);
    size_t first_space = first_line.find(' ');
    if (first_space != std::string::npos) {
      size_t second_space = first_line.find(' ', first_space + 1);
      std::string code_str =
          first_line.substr(first_space + 1, second_space - first_space - 1);
      status_code = std::atoi(code_str.c_str());
    }
  }

  // Check if Redirect
  if (status_code >= 300 && status_code < 400) {
    size_t loc = headers.find("Location:");
    if (loc == std::string::npos) loc = headers.find("location:");
    if (loc != std::string::npos) {
      return true;
    }
  }

  // 204 No Content and 304 Not Modified responses have no body.
  if (status_code == 204 || status_code == 304) return true;

  // Check for Content-Length
  size_t cl_pos = headers.find("Content-Length:");
  if (cl_pos == std::string::npos) cl_pos = headers.find("content-length:");
  if (cl_pos != std::string::npos) {
    size_t next_line = headers.find("\n", cl_pos);
    std::string line = headers.substr(cl_pos, next_line != std::string::npos
                                                  ? (next_line - cl_pos)
                                                  : std::string::npos);
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
      long expected_len = std::atol(line.substr(colon + 1).c_str());
      if (body.length() >= (size_t)expected_len) {
        return true;
      }
    }
  }

  // Check for Transfer-Encoding: chunked
  size_t te_pos = headers.find("Transfer-Encoding:");
  if (te_pos == std::string::npos) te_pos = headers.find("transfer-encoding:");
  if (te_pos != std::string::npos) {
    size_t next_line = headers.find("\n", te_pos);
    std::string line = headers.substr(te_pos, next_line != std::string::npos
                                                  ? (next_line - te_pos)
                                                  : std::string::npos);
    if (line.find("chunked") != std::string::npos ||
        line.find("Chunked") != std::string::npos) {
      size_t pos = 0;
      while (pos < body.length()) {
        size_t next_rnl = body.find("\r\n", pos);
        if (next_rnl == std::string::npos) break;
        std::string hex_str = body.substr(pos, next_rnl - pos);
        size_t semi = hex_str.find(';');
        if (semi != std::string::npos) {
          hex_str = hex_str.substr(0, semi);
        }
        size_t chunk_size = ParseHex(hex_str);
        if (chunk_size == 0) {
          return true;  // Terminal chunk reached
        }
        pos = next_rnl + 2 + chunk_size + 2;
      }
    }
  }

  return false;
}

static void process_http_response(http_fetch_context* ctx) {
  if (ctx->freed || ctx->finished) return;
  ctx->finished = true;
  ctx->active = false;
  if (ctx->sock >= 0) {
    close(ctx->sock);
    ctx->sock = -1;
  }

  if (ctx->response_data.empty()) {
    fetch_remove_from_queues(ctx->parent_fetch);
    fetch_msg msg;
    msg.type = FETCH_ERROR;
    msg.data.error = "EmptyHttpResponse";
    fetch_send_callback(&msg, ctx->parent_fetch);
    return;
  }

  std::string& resp = ctx->response_data;
  size_t ddelim = resp.find("\r\n\r\n");
  if (ddelim == std::string::npos) {
    ddelim = resp.find("\n\n");
  }

  std::string headers = "";
  std::string body = resp;
  if (ddelim != std::string::npos) {
    headers = resp.substr(0, ddelim);
    size_t delim_len = (resp.compare(ddelim, 4, "\r\n\r\n") == 0) ? 4 : 2;
    body = resp.substr(ddelim + delim_len);
  }

  bool is_chunked = false;
  size_t te_pos = headers.find("Transfer-Encoding:");
  if (te_pos == std::string::npos) {
    te_pos = headers.find("transfer-encoding:");
  }
  if (te_pos != std::string::npos) {
    size_t next_line = headers.find("\n", te_pos);
    std::string line = headers.substr(te_pos, next_line != std::string::npos
                                                  ? (next_line - te_pos)
                                                  : std::string::npos);
    if (line.find("chunked") != std::string::npos ||
        line.find("Chunked") != std::string::npos) {
      is_chunked = true;
    }
  }

  if (is_chunked) {
    std::string decoded = "";
    size_t pos = 0;
    while (pos < body.length()) {
      size_t next_line = body.find("\r\n", pos);
      if (next_line == std::string::npos) break;
      std::string hex_str = body.substr(pos, next_line - pos);
      size_t semi = hex_str.find(';');
      if (semi != std::string::npos) {
        hex_str = hex_str.substr(0, semi);
      }
      size_t chunk_size = ParseHex(hex_str);
      if (chunk_size == 0) {
        break;
      }
      pos = next_line + 2;
      if (pos + chunk_size > body.length()) {
        decoded.append(body.substr(pos));
        break;
      }
      decoded.append(body.substr(pos, chunk_size));
      pos += chunk_size + 2;  // skip chunk data and trailing \r\n
    }
    body = decoded;
  }

  long status_code = 200;
  size_t first_line_end = headers.find("\r\n");
  if (first_line_end == std::string::npos) first_line_end = headers.find("\n");
  if (first_line_end != std::string::npos) {
    std::string first_line = headers.substr(0, first_line_end);
    size_t first_space = first_line.find(' ');
    if (first_space != std::string::npos) {
      size_t second_space = first_line.find(' ', first_space + 1);
      std::string code_str =
          first_line.substr(first_space + 1, second_space - first_space - 1);
      status_code = std::atoi(code_str.c_str());
    }
  }

  if (status_code >= 300 && status_code < 400) {
    std::string redirect_url = "";
    size_t pos = 0;
    while (pos < headers.length()) {
      size_t next_newline = headers.find("\n", pos);
      std::string line;
      if (next_newline != std::string::npos) {
        line = headers.substr(pos, next_newline - pos);
        pos = next_newline + 1;
      } else {
        line = headers.substr(pos);
        pos = headers.length();
      }
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      size_t colon = line.find(':');
      if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        while (!key.empty() && (key.front() == ' ' || key.front() == '\t'))
          key.erase(key.begin());
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
          key.pop_back();
        for (auto& c : key) {
          if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        }

        if (key == "location") {
          std::string val = line.substr(colon + 1);
          while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
            val.erase(val.begin());
          while (!val.empty() && (val.back() == ' ' || val.back() == '\t'))
            val.pop_back();
          redirect_url = val;
          break;
        }
      }
    }
    if (!redirect_url.empty()) {
      fetch_set_http_code(ctx->parent_fetch, (http_response_code)status_code);

      fetch_msg msg;
      msg.type = FETCH_REDIRECT;
      msg.data.redirect = redirect_url.c_str();
      fetch_send_callback(&msg, ctx->parent_fetch);
      return;
    }
  }

  fetch_set_http_code(
      ctx->parent_fetch,
      (http_response_code)(status_code == 403 ? 200 : status_code));

  size_t start = 0;
  while (start < headers.length()) {
    size_t end = headers.find("\r\n", start);
    size_t next_start = 0;
    if (end != std::string::npos) {
      next_start = end + 2;
    } else {
      end = headers.find("\n", start);
      if (end != std::string::npos) {
        next_start = end + 1;
      }
    }

    if (end == std::string::npos) {
      std::string h = headers.substr(start) + "\r\n";
      fetch_msg msg;
      msg.type = FETCH_HEADER;
      msg.data.header_or_data.buf = (const uint8_t*)h.data();
      msg.data.header_or_data.len = h.length();
      fetch_send_callback(&msg, ctx->parent_fetch);
      if (ctx->freed) return;
      break;
    }

    std::string h = headers.substr(start, next_start - start);
    fetch_msg msg;
    msg.type = FETCH_HEADER;
    msg.data.header_or_data.buf = (const uint8_t*)h.data();
    msg.data.header_or_data.len = h.length();
    fetch_send_callback(&msg, ctx->parent_fetch);
    if (ctx->freed) return;
    start = next_start;
  }

  // Send the blank line indicating end of headers
  {
    fetch_msg msg;
    msg.type = FETCH_HEADER;
    msg.data.header_or_data.buf = (const uint8_t*)"\r\n";
    msg.data.header_or_data.len = 2;
    fetch_send_callback(&msg, ctx->parent_fetch);
    if (ctx->freed) return;
  }

  if (!body.empty()) {
    fetch_msg msg;
    msg.type = FETCH_DATA;
    msg.data.header_or_data.buf = (const uint8_t*)body.data();
    msg.data.header_or_data.len = body.length();
    fetch_send_callback(&msg, ctx->parent_fetch);
    if (ctx->freed) return;
  }

  fetch_msg msg;
  msg.type = FETCH_FINISHED;
  fetch_send_callback(&msg, ctx->parent_fetch);
}

static void run_ssl_engine(http_fetch_context* ctx) {
  for (;;) {
    if (ctx->freed) return;
    unsigned int state = br_ssl_engine_current_state(&ctx->sc.eng);

    if (state & BR_SSL_CLOSED) {
      int err = br_ssl_engine_last_error(&ctx->sc.eng);
      std::cout << "SSL engine is closed. Last error code: " << (int64)err
                << std::endl;
      if (err != BR_ERR_OK && !ctx->socket_closed &&
          ctx->response_data.empty()) {
        ctx->failed = true;
        ctx->failure_reason = "SslHandshakeOrConnectionFailed";
      } else {
        process_http_response(ctx);
      }
      return;
    }

    if (ctx->socket_closed && !(state & BR_SSL_RECVAPP)) {
      std::cout << "SSL socket closed and no more decrypted "
                   "application data. Processing response."
                << std::endl;
      process_http_response(ctx);
      return;
    }

    bool did_work = false;

    // Send encrypted data to socket
    if (state & BR_SSL_SENDREC) {
      size_t len;
      unsigned char* buf = br_ssl_engine_sendrec_buf(&ctx->sc.eng, &len);
      if (len > 0) {
        if (ctx->socket_closed) {
          std::cout << "NetSurf SSL: Socket closed, discarding " << (int64)len
                    << " bytes of outgoing record\n";
          br_ssl_engine_sendrec_ack(&ctx->sc.eng, len);
          did_work = true;
        } else {
          ssize_t sent = write(ctx->sock, buf, len);
          if (sent < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
              std::cout << "NetSurf SSL: Socket write failed! errno = "
                        << (int64)errno << std::endl;
              if (errno == ECONNRESET || errno == EPIPE) {
                std::cout << "NetSurf SSL: Connection closed by peer. Ignoring "
                             "close/write error.\n";
                br_ssl_engine_sendrec_ack(&ctx->sc.eng, len);
                did_work = true;
              } else {
                ctx->failed = true;
                ctx->failure_reason = "SocketWriteFailed";
                return;
              }
            }
          } else if (sent > 0) {
            br_ssl_engine_sendrec_ack(&ctx->sc.eng, sent);
            did_work = true;
          }
        }
      }
    }

    // Read encrypted data from socket
    if ((state & BR_SSL_RECVREC) && !ctx->socket_closed) {
      size_t len;
      unsigned char* buf = br_ssl_engine_recvrec_buf(&ctx->sc.eng, &len);
      if (len > 0) {
        ssize_t recved = read(ctx->sock, buf, len);
        if (recved < 0) {
          if (errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cout << "SSL socket read failed! recved = " << (int64)recved
                      << ", errno = " << (int64)errno << std::endl;
            ctx->failed = true;
            ctx->failure_reason = "SocketReadFailed";
            return;
          }
        } else if (recved == 0) {
          std::cout << "SSL socket EOF during read\n";
          ctx->socket_closed = true;
          br_ssl_engine_close(&ctx->sc.eng);
          did_work = true;
        } else {
          br_ssl_engine_recvrec_ack(&ctx->sc.eng, recved);
          did_work = true;
        }
      }
    }

    // Send plaintext app request data to SSL engine
    if (state & BR_SSL_SENDAPP) {
      size_t len;
      unsigned char* buf = br_ssl_engine_sendapp_buf(&ctx->sc.eng, &len);
      if (ctx->sent_bytes < ctx->request_data.length()) {
        size_t to_send =
            std::min(len, ctx->request_data.length() - ctx->sent_bytes);
        if (to_send > 0) {
          std::memcpy(buf, ctx->request_data.data() + ctx->sent_bytes, to_send);
          br_ssl_engine_sendapp_ack(&ctx->sc.eng, to_send);
          ctx->sent_bytes += to_send;
          did_work = true;
          if (ctx->sent_bytes >= ctx->request_data.length()) {
            br_ssl_engine_flush(&ctx->sc.eng, 0);
          }
        }
      }
    }

    // Read decrypted plaintext response data from SSL engine
    if (state & BR_SSL_RECVAPP) {
      size_t len;
      unsigned char* buf = br_ssl_engine_recvapp_buf(&ctx->sc.eng, &len);
      if (len > 0) {
        ctx->response_data.append((const char*)buf, len);
        br_ssl_engine_recvapp_ack(&ctx->sc.eng, len);
        did_work = true;
        if (is_http_response_complete(ctx)) {
          process_http_response(ctx);
          return;
        }
      }
    }

    if (!did_work) {
      // If no progress-making operation occurred in this loop iteration,
      // return to avoid spinning.
      return;
    }
  }
}

static void http_fetch_poll(lwc_string* scheme) {
  std::vector<http_fetch_context*> fetches_to_process;
  {
    std::scoped_lock lock(active_fetches_mutex);
    fetches_to_process = active_http_fetches;
  }
  for (auto ctx : fetches_to_process) {
    if (ctx->freed) {
      if (!ctx->in_poll) delete ctx;
      continue;
    }
    if (!ctx->active || ctx->finished) continue;

    ctx->in_poll = true;

    if (ctx->failed) {
      ctx->active = false;
      fetch_remove_from_queues(ctx->parent_fetch);
      fetch_msg msg;
      msg.type = FETCH_ERROR;
      msg.data.error = ctx->failure_reason.c_str();
      fetch_send_callback(&msg, ctx->parent_fetch);
      ctx->in_poll = false;
      if (ctx->freed) delete ctx;
      continue;
    }

    if (ctx->is_https) {
      run_ssl_engine(ctx);
      ctx->in_poll = false;
      if (ctx->freed) delete ctx;
      continue;
    }

    if (ctx->sent_bytes < ctx->request_data.length()) {
      ssize_t sent =
          write(ctx->sock, ctx->request_data.data() + ctx->sent_bytes,
                ctx->request_data.length() - ctx->sent_bytes);
      if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          ctx->in_poll = false;
          continue;
        }
        ctx->active = false;
        fetch_remove_from_queues(ctx->parent_fetch);

        fetch_msg msg;
        msg.type = FETCH_ERROR;
        msg.data.error = "SocketWriteFailed";
        fetch_send_callback(&msg, ctx->parent_fetch);
        ctx->in_poll = false;
        if (ctx->freed) delete ctx;
        continue;
      }
      ctx->sent_bytes += sent;
    }

    if (ctx->sent_bytes >= ctx->request_data.length()) {
      char buf[2048];
      ssize_t recved = read(ctx->sock, buf, sizeof(buf));
      if (recved < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          ctx->in_poll = false;
          continue;
        }
        ctx->active = false;
        fetch_remove_from_queues(ctx->parent_fetch);

        fetch_msg msg;
        msg.type = FETCH_ERROR;
        msg.data.error = "SocketReadFailed";
        fetch_send_callback(&msg, ctx->parent_fetch);
        ctx->in_poll = false;
        if (ctx->freed) delete ctx;
        continue;
      } else if (recved == 0) {
        ctx->socket_closed = true;
        process_http_response(ctx);
      } else {
        ctx->response_data.append(buf, recved);
        if (is_http_response_complete(ctx)) {
          process_http_response(ctx);
        }
      }
    }

    ctx->in_poll = false;
    if (ctx->freed) delete ctx;
  }
}

void RegisterPerceptionHttpFetcher() {
  lwc_string* http_scheme = nullptr;
  lwc_intern_string("http", 4, &http_scheme);

  static const struct fetcher_operation_table http_fetch_ops = {
      .initialise = http_fetch_initialise,
      .acceptable = http_fetch_acceptable,
      .setup = http_fetch_setup,
      .start = http_fetch_start,
      .abort = http_fetch_abort,
      .free = http_fetch_free,
      .poll = http_fetch_poll,
      .finalise = http_fetch_finalise};

  if (auto error = fetcher_add(http_scheme, &http_fetch_ops);
      error != NSERROR_OK) {
    std::cout << "Failed to register NetSurf Custom HTTP Fetcher: "
              << (int64)error << std::endl;
  }

  lwc_string* https_scheme = nullptr;
  lwc_intern_string("https", 5, &https_scheme);
  if (auto error = fetcher_add(https_scheme, &http_fetch_ops);
      error != NSERROR_OK) {
    std::cout << "Failed to register NetSurf Custom HTTPS Fetcher: "
              << (int64)error << std::end;
  }
}

}  // namespace perception
}  // namespace netsurf
