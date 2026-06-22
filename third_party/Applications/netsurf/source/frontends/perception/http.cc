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
#include "perception/debug.h"
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
  std::cout << "unsafe_start_chain for " << (server_name ? server_name : "NULL")
            << std::endl;
  uc->minimal.vtable->start_chain(&uc->minimal.vtable, server_name);
}
static void unsafe_start_cert(const br_x509_class** ctx, uint32_t length) {
  br_x509_unsafe_context* uc = (br_x509_unsafe_context*)ctx;
  std::cout << "unsafe_start_cert length = " << length
            << ", err = " << uc->minimal.err << std::endl;
  uc->minimal.vtable->start_cert(&uc->minimal.vtable, length);
}
static void unsafe_append(const br_x509_class** ctx, const unsigned char* buf,
                          size_t len) {
  br_x509_unsafe_context* uc = (br_x509_unsafe_context*)ctx;
  uc->minimal.vtable->append(&uc->minimal.vtable, buf, len);
  if (uc->minimal.err != 0) {
    std::cout << "unsafe_append err = " << uc->minimal.err << std::endl;
  }
}
static void unsafe_end_cert(const br_x509_class** ctx) {
  br_x509_unsafe_context* uc = (br_x509_unsafe_context*)ctx;
  uc->minimal.vtable->end_cert(&uc->minimal.vtable);
  std::cout << "unsafe_end_cert err = " << uc->minimal.err << std::endl;
}
static unsigned unsafe_end_chain(const br_x509_class** ctx) {
  br_x509_unsafe_context* uc = (br_x509_unsafe_context*)ctx;
  unsigned err = uc->minimal.vtable->end_chain(&uc->minimal.vtable);
  std::cout << "unsafe_end_chain minimal_err = " << uc->minimal.err
            << ", end_chain returned = " << err << std::endl;
  return 0;  // Ignore all verification errors
}
static const br_x509_pkey* unsafe_get_pkey(const br_x509_class* const* ctx,
                                           unsigned* usages) {
  br_x509_unsafe_context* uc = (br_x509_unsafe_context*)ctx;
  std::cout << "unsafe_get_pkey: minimal.pkey.key_type = "
            << (int)uc->minimal.pkey.key_type << std::endl;
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
  unsigned char io_buffer[BR_SSL_BUFSIZE_MONO];
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

  ctx->request_data =
      "GET " + ctx->path_and_query + " HTTP/1.1\r\n" + "Host: " + ctx->host +
      "\r\n" + "User-Agent: NetSurf/3.11 (Perception; x86_64)\r\n" +
      "Accept: "
      "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n" +
      "Accept-Language: en-US,en;q=0.5\r\n" + "Connection: close\r\n\r\n";

  if (ctx->is_https) {
    // Initialize X509 unsafe engine
    ctx->uc.vtable = &br_x509_unsafe_vtable;

    // Initialize SSL client context
    br_ssl_client_init_full(&ctx->sc, &ctx->uc.minimal, nullptr, 0);

    time_t current_time = time(nullptr);
    std::cout << "NetSurf HTTP Fetcher: time(nullptr) returned " << current_time
              << std::endl;
    if (current_time != (time_t)-1) {
      ctx->uc.minimal.days = (uint32_t)(current_time / 86400) + 719528;
      ctx->uc.minimal.seconds = (uint32_t)(current_time % 86400);
      std::cout << "NetSurf HTTP Fetcher: set days = " << ctx->uc.minimal.days
                << ", seconds = " << ctx->uc.minimal.seconds << std::endl;
    }

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

    // Set buffer
    br_ssl_engine_set_buffer(&ctx->sc.eng, ctx->io_buffer,
                             sizeof(ctx->io_buffer), 0);

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

  if (ctx->is_https) {
    int flags = fcntl(ctx->sock, F_GETFL, 0);
    if (flags >= 0) {
      fcntl(ctx->sock, F_SETFL, flags | O_NONBLOCK);
    }
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
  auto it =
      std::find(active_http_fetches.begin(), active_http_fetches.end(), ctx);
  if (it != active_http_fetches.end()) {
    active_http_fetches.erase(it);
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
      if (err != BR_ERR_OK && !ctx->socket_closed) {
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

    // 1. Send encrypted data to socket
    if (state & BR_SSL_SENDREC) {
      size_t len;
      unsigned char* buf = br_ssl_engine_sendrec_buf(&ctx->sc.eng, &len);
      if (len > 0) {
        if (ctx->socket_closed) {
          std::cout << "NetSurf SSL: Socket closed, discarding " << len
                    << " bytes of outgoing record" << std::endl;
          br_ssl_engine_sendrec_ack(&ctx->sc.eng, len);
          did_work = true;
        } else {
          std::cout << "NetSurf SSL: Writing " << len
                    << " encrypted bytes to socket " << ctx->sock << std::endl;
          ssize_t sent = write(ctx->sock, buf, len);
          if (sent < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
              std::cout << "NetSurf SSL: Socket write failed! errno = " << errno
                        << std::endl;
              if (errno == ECONNRESET || errno == EPIPE) {
                std::cout << "NetSurf SSL: Connection closed by peer. Ignoring "
                             "close/write error."
                          << std::endl;
                br_ssl_engine_sendrec_ack(&ctx->sc.eng, len);
                did_work = true;
              } else {
                ctx->failed = true;
                ctx->failure_reason = "SocketWriteFailed";
                return;
              }
            }
          } else if (sent > 0) {
            std::cout << "NetSurf SSL: Wrote " << sent << " bytes to socket"
                      << std::endl;
            br_ssl_engine_sendrec_ack(&ctx->sc.eng, sent);
            did_work = true;
          }
        }
      }
    }

    // 2. Read encrypted data from socket
    if ((state & BR_SSL_RECVREC) && !ctx->socket_closed) {
      size_t len;
      unsigned char* buf = br_ssl_engine_recvrec_buf(&ctx->sc.eng, &len);
      if (len > 0) {
        ssize_t recved = read(ctx->sock, buf, len);
        if (recved < 0) {
          if (errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cout << "SSL socket read failed! recved = " << recved
                      << ", errno = " << errno << std::endl;
            ctx->failed = true;
            ctx->failure_reason = "SocketReadFailed";
            return;
          }
        } else if (recved == 0) {
          std::cout << "SSL socket EOF during read" << std::endl;
          ctx->socket_closed = true;
          br_ssl_engine_close(&ctx->sc.eng);
          did_work = true;
        } else {
          br_ssl_engine_recvrec_ack(&ctx->sc.eng, recved);
          did_work = true;
        }
      }
    }

    // 3. Send plaintext app request data to SSL engine
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

    // 4. Read decrypted plaintext response data from SSL engine
    if (state & BR_SSL_RECVAPP) {
      size_t len;
      unsigned char* buf = br_ssl_engine_recvapp_buf(&ctx->sc.eng, &len);
      if (len > 0) {
        std::cout << "NetSurf SSL: Received " << len
                  << " decrypted plaintext bytes from SSL engine" << std::endl;
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
      // If we didn't perform any progress-making operation in this loop
      // iteration, we must break to avoid an infinite loop.
      return;
    }
  }
}

static void http_fetch_poll(lwc_string* scheme) {
  auto fetches_to_process = active_http_fetches;
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
              << (int)error << std::endl;
  }

  lwc_string* https_scheme = nullptr;
  lwc_intern_string("https", 5, &https_scheme);
  if (auto error = fetcher_add(https_scheme, &http_fetch_ops);
      error != NSERROR_OK) {
    std::cout << "Failed to register NetSurf Custom HTTPS Fetcher: "
              << (int)error << std::endl;
  }
}

}  // namespace perception
}  // namespace netsurf
