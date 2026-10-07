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
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "bearssl.h"
#include "perception/fibers.h"
#include "perception/http/http2.h"
#include "perception/network/ip_address.h"
#include "perception/network/network_service.h"
#include "perception/posix_network.h"
#include "perception/processes.h"
#include "perception/random.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/time.h"
#include "network_log.h"
#include "utils/errors.h"

extern "C" {
#include <nsutils/base64.h>

#include "content/fetch.h"
#include "content/fetchers.h"
#include "content/urldb.h"
#include "netsurf/fetch.h"
#include "utils/log.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"
}

namespace netsurf {
namespace perception {
namespace {

// Maximum number of concurrent active sockets to avoid exhausting lwIP's TCP PCB pool.
constexpr size_t kMaxConcurrentSockets = 8;

// Size of the read buffer for plain HTTP socket reads.
constexpr size_t kHttpReadBufferSize = 16384;

// Number of bytes of secure entropy to inject into the BearSSL engine.
constexpr size_t kSslEntropyBytes = 32;

// Default port for plain HTTP connections.
constexpr int kHttpPort = 80;

// Default port for HTTPS connections.
constexpr int kHttpsPort = 443;

// Default port for HTTP proxy connections when unset in options.
constexpr int kDefaultProxyPort = 8080;

// Option value representing HTTP Basic proxy authentication.
constexpr int kProxyAuthBasic = 1;

// Buffer size used when reading local files for multipart form uploads.
constexpr size_t kFileUploadReadBufferSize = 4096;

// Maximum bytes to read for an HTTP CONNECT proxy response header block.
constexpr size_t kMaxConnectResponseBytes = 8192;

// Poll interval while waiting for an available socket slot.
constexpr auto kSocketSlotWaitInterval = std::chrono::milliseconds(20);

// Delay between TCP connect retry attempts.
constexpr auto kConnectRetryDelay = std::chrono::milliseconds(100);

// ALPN protocol identifier for HTTP/2 over TLS (RFC 9113 Section 3.3).
constexpr std::string_view kAlpnH2 = "h2";

// Number of ALPN protocols advertised in the TLS ClientHello.
constexpr size_t kAlpnProtocolCount = 2;

// Default User-Agent header value sent when not provided by the caller.
constexpr std::string_view kDefaultUserAgent =
    "Mozilla/5.0 (X11; Perception x86_64) NetSurf/3.11";

// Default Accept header value sent when not provided by the caller.
constexpr std::string_view kDefaultAccept =
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,"
    "image/png,image/svg+xml,image/*;q=0.8,*/*;q=0.7";

// Default Accept-Language header value sent when not provided by the caller.
constexpr std::string_view kDefaultAcceptLanguage = "en-US,en;q=0.5";

// Header name prefix (including colon) of a request Content-Length header.
constexpr std::string_view kContentLengthPrefix = "Content-Length:";

// Multipart boundary string used for multipart/form-data POST bodies.
constexpr std::string_view kMultipartBoundary =
    "----NetSurfPerceptionBoundary7MA4YWxkTrZu0gW";

// Ordered list of ALPN protocols advertised by the HTTPS client.
const char* kAlpnProtocols[kAlpnProtocolCount] = {"h2", "http/1.1"};

static std::atomic<size_t> active_socket_count{0};

std::string ToLowerAsciiString(std::string_view sv) {
  std::string out(sv);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

std::string TrimAsciiWhitespace(std::string_view sv) {
  size_t start = 0;
  while (start < sv.size() &&
         (sv[start] == ' ' || sv[start] == '\t' || sv[start] == '\r' ||
          sv[start] == '\n'))
    ++start;
  size_t end = sv.size();
  while (end > start &&
         (sv[end - 1] == ' ' || sv[end - 1] == '\t' || sv[end - 1] == '\r' ||
          sv[end - 1] == '\n'))
    --end;
  return std::string(sv.substr(start, end - start));
}

bool ShouldUseHttpProxy(std::string_view host) {
  if (!nsoption_bool(http_proxy))
    return false;
  const char* proxy_host = nsoption_charp(http_proxy_host);
  if (proxy_host == nullptr || proxy_host[0] == '\0')
    return false;

  const char* no_proxy = nsoption_charp(http_proxy_noproxy);
  if (no_proxy == nullptr || no_proxy[0] == '\0')
    return true;

  std::string lower_host = ToLowerAsciiString(host);
  std::string_view list(no_proxy);
  size_t pos = 0;
  while (pos < list.size()) {
    size_t comma = list.find(',', pos);
    if (comma == std::string_view::npos)
      comma = list.size();
    std::string pattern =
        ToLowerAsciiString(TrimAsciiWhitespace(list.substr(pos, comma - pos)));
    pos = comma + 1;

    if (pattern.empty())
      continue;
    if (pattern == "*")
      return false;

    std::string_view clean_pattern = pattern;
    if (clean_pattern.front() == '.')
      clean_pattern.remove_prefix(1);
    if (clean_pattern.empty())
      continue;

    if (lower_host == clean_pattern)
      return false;
    if (lower_host.size() > clean_pattern.size() &&
        lower_host.compare(lower_host.size() - clean_pattern.size(),
                           clean_pattern.size(), clean_pattern) == 0 &&
        lower_host[lower_host.size() - clean_pattern.size() - 1] == '.')
      return false;
  }
  return true;
}

std::string BuildProxyAuthorizationValue() {
  if (nsoption_int(http_proxy_auth) != kProxyAuthBasic)
    return "";
  const char* user = nsoption_charp(http_proxy_auth_user);
  if (user == nullptr || user[0] == '\0')
    return "";
  const char* pass = nsoption_charp(http_proxy_auth_pass);
  std::string credentials =
      std::string(user) + ":" + (pass != nullptr ? pass : "");
  uint8_t* encoded = nullptr;
  size_t encoded_len = 0;
  if (nsu_base64_encode_alloc(
          reinterpret_cast<const uint8_t*>(credentials.data()),
          credentials.size(), &encoded, &encoded_len) != NSUERROR_OK ||
      encoded == nullptr)
    return "";
  std::string header_val =
      "Basic " + std::string(reinterpret_cast<char*>(encoded), encoded_len);
  free(encoded);
  return header_val;
}

std::string ExtractFilenameFromPath(std::string_view path) {
  size_t slash = path.find_last_of("/\\");
  if (slash == std::string_view::npos)
    return std::string(path);
  return std::string(path.substr(slash + 1));
}

std::string GuessMimeTypeForFilename(std::string_view filename) {
  size_t dot = filename.rfind('.');
  if (dot == std::string_view::npos)
    return "application/octet-stream";
  std::string ext = ToLowerAsciiString(filename.substr(dot));
  if (ext == ".txt" || ext == ".log")
    return "text/plain";
  if (ext == ".htm" || ext == ".html")
    return "text/html";
  if (ext == ".css")
    return "text/css";
  if (ext == ".js")
    return "application/javascript";
  if (ext == ".json")
    return "application/json";
  if (ext == ".xml")
    return "application/xml";
  if (ext == ".png")
    return "image/png";
  if (ext == ".jpg" || ext == ".jpeg")
    return "image/jpeg";
  if (ext == ".gif")
    return "image/gif";
  if (ext == ".svg")
    return "image/svg+xml";
  if (ext == ".webp")
    return "image/webp";
  if (ext == ".pdf")
    return "application/pdf";
  return "application/octet-stream";
}

bool ReadLocalFileBytes(const char* path, std::string& out) {
  if (path == nullptr || path[0] == '\0')
    return false;
  FILE* fp = fopen(path, "rb");
  if (fp == nullptr)
    return false;
  char buffer[kFileUploadReadBufferSize];
  size_t bytes_read = 0;
  while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    out.append(buffer, bytes_read);
  fclose(fp);
  return true;
}

std::string FormatTlsInfo(br_ssl_engine_context* eng) {
  unsigned int version = br_ssl_engine_get_version(eng);
  std::string version_str = "TLS";
  if (version == BR_TLS12)
    version_str = "TLS 1.2";
  else if (version == BR_TLS11)
    version_str = "TLS 1.1";
  else if (version == BR_TLS10)
    version_str = "TLS 1.0";

  br_ssl_session_parameters params;
  br_ssl_engine_get_session_parameters(eng, &params);
  char cipher_buf[32];
  snprintf(cipher_buf, sizeof(cipher_buf), "0x%04X",
           static_cast<unsigned int>(params.cipher_suite));

  const char* alpn = br_ssl_engine_get_selected_protocol(eng);
  std::string alpn_str = (alpn != nullptr && alpn[0] != '\0') ? alpn : "http/1.1";
  return version_str + " (Cipher " + cipher_buf + ", ALPN " + alpn_str + ")";
}

}  // namespace

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
  if (usages != nullptr)
    *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
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
  struct nsurl* url = nullptr;
  std::string host;
  int port;
  bool use_proxy = false;
  std::string proxy_host;
  int proxy_port = kDefaultProxyPort;
  std::string proxy_auth_header;
  std::string path_and_query;
  std::string method;
  std::vector<::perception::http::HeaderField> request_headers;
  std::string post_body;
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
  bool fetch_freed = false;

  std::atomic<int> ref_count{1};
  bool removed_from_queues = false;
  bool socket_counted = false;

  bool connecting_in_background = false;
  bool connect_failed = false;
  bool free_deferred = false;

  // HTTPS and HTTP/2 (ALPN "h2") Support
  bool is_https;
  bool socket_closed;
  bool tls_session_saved = false;
  bool alpn_checked = false;
  bool is_http2 = false;
  uint32_t h2_stream_id = 0;
  ::perception::http::Http2ClientSession h2_session;
  br_ssl_client_context sc;
  br_x509_unsafe_context uc;
  unsigned char io_buffer[BR_SSL_BUFSIZE_BIDI];
};

static std::unordered_map<std::string, br_ssl_session_parameters> tls_session_cache;
static std::mutex active_fetches_mutex;
static std::vector<http_fetch_context*> active_http_fetches;

static void RemoveFetchFromQueuesOnce(http_fetch_context* ctx) {
  if (ctx != nullptr && !ctx->removed_from_queues && ctx->parent_fetch != nullptr) {
    ctx->removed_from_queues = true;
    fetch_remove_from_queues(ctx->parent_fetch);
  }
}

static void FreeParentFetchOnce(http_fetch_context* ctx) {
  RemoveFetchFromQueuesOnce(ctx);
  if (!ctx->fetch_freed && ctx->parent_fetch != nullptr) {
    ctx->fetch_freed = true;
    struct fetch* parent = ctx->parent_fetch;
    ctx->parent_fetch = nullptr;
    fetch_free(parent);
  }
}

static void CloseContextSocket(http_fetch_context* ctx) {
  if (ctx->sock >= 0) {
    close(ctx->sock);
    ctx->sock = -1;
  }
  if (ctx->socket_counted) {
    ctx->socket_counted = false;
    size_t count = active_socket_count.load();
    while (count > 0 &&
           !active_socket_count.compare_exchange_weak(count, count - 1)) {
    }
  }
}

static void ReleaseContext(http_fetch_context* ctx) {
  if (ctx->ref_count.fetch_sub(1) == 1) {
    CloseContextSocket(ctx);
    if (ctx->url != nullptr) {
      nsurl_unref(ctx->url);
      ctx->url = nullptr;
    }
    delete ctx;
  }
}

static void init_ssl_context(http_fetch_context* ctx) {
  // Initialize X509 unsafe engine.
  ctx->uc.vtable = &br_x509_unsafe_vtable;

  // Initialize SSL client context.
  br_ssl_client_init_full(&ctx->sc, &ctx->uc.minimal, nullptr, 0);

  // Set time callback to return 0 so certificate validity dates always pass
  // and certificate parsing succeeds in extracting public keys.
  br_x509_minimal_set_time_callback(
      &ctx->uc.minimal, nullptr,
      [](void*, uint32_t, uint32_t, uint32_t, uint32_t) -> int { return 0; });

  // Set unsafe X509 engine.
  br_ssl_engine_set_x509(&ctx->sc.eng, &ctx->uc.vtable);

  // Advertise ALPN support for HTTP/2 ("h2") with fallback to "http/1.1".
  br_ssl_engine_set_protocol_names(&ctx->sc.eng, kAlpnProtocols,
                                   kAlpnProtocolCount);

  // Inject secure entropy before reset, because br_ssl_client_reset
  // initializes the PRNG.
  unsigned char entropy[kSslEntropyBytes];
  for (size_t i = 0; i < kSslEntropyBytes; i += sizeof(size_t)) {
    size_t r = ::perception::RandomNumber();
    std::memcpy(entropy + i, &r, std::min(kSslEntropyBytes - i, sizeof(r)));
  }
  br_ssl_engine_inject_entropy(&ctx->sc.eng, entropy, kSslEntropyBytes);

  // Set buffer to bidirectional.
  br_ssl_engine_set_buffer(&ctx->sc.eng, ctx->io_buffer,
                           sizeof(ctx->io_buffer), 1);

  bool resume = false;
  {
    std::scoped_lock lock(active_fetches_mutex);
    auto it = tls_session_cache.find(ctx->host);
    if (it != tls_session_cache.end()) {
      br_ssl_engine_set_session_parameters(&ctx->sc.eng, &it->second);
      resume = true;
    }
  }

  // RFC 6066 Section 3 forbids literal IPv4 and IPv6 addresses in TLS SNI.
  const char* server_name =
      ::perception::network::IpAddress::Parse(ctx->host).has_value()
          ? nullptr
          : ctx->host.c_str();
  br_ssl_client_reset(&ctx->sc, server_name, resume ? 1 : 0);
}

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
  ctx->url = (url != nullptr) ? nsurl_ref(url) : nullptr;
  ctx->sock = -1;
  ctx->active = false;
  ctx->finished = false;
  ctx->sent_bytes = 0;
  ctx->failed = false;
  ctx->failure_reason = "";
  ctx->socket_closed = false;
  ctx->in_poll = false;
  ctx->freed = false;
  ctx->fetch_freed = false;
  ctx->ref_count = 1;
  ctx->removed_from_queues = false;
  ctx->socket_counted = false;
  ctx->connecting_in_background = false;
  ctx->connect_failed = false;
  ctx->free_deferred = false;
  ctx->tls_session_saved = false;

  ctx->is_https = false;
  lwc_string* scheme_lwc = nsurl_get_component(url, NSURL_SCHEME);
  if (scheme_lwc) {
    std::string scheme_str = lwc_string_data(scheme_lwc);
    if (scheme_str == "https")
      ctx->is_https = true;
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
    ctx->port = ctx->is_https ? kHttpsPort : kHttpPort;
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

  ctx->use_proxy = ShouldUseHttpProxy(ctx->host);
  if (ctx->use_proxy) {
    ctx->proxy_host = nsoption_charp(http_proxy_host);
    int configured_proxy_port = nsoption_int(http_proxy_port);
    ctx->proxy_port =
        (configured_proxy_port > 0) ? configured_proxy_port : kDefaultProxyPort;
    ctx->proxy_auth_header = BuildProxyAuthorizationValue();
  }

  std::string method =
      (post_urlenc != nullptr || post_multipart != nullptr) ? "POST" : "GET";
  ctx->method = method;

  std::string request_target = ctx->path_and_query;
  if (ctx->use_proxy && !ctx->is_https && url != nullptr &&
      nsurl_access(url) != nullptr)
    request_target = nsurl_access(url);

  ctx->request_data = method + " " + request_target + " HTTP/1.1\r\n";
  ctx->request_data += "Host: " + ctx->host + "\r\n";

  bool has_user_agent = false;
  bool has_accept = false;
  bool has_accept_language = false;
  bool has_accept_charset = false;
  bool has_dnt = false;
  bool has_connection = false;
  bool has_content_type = false;
  bool has_content_length = false;
  bool has_cookie = false;
  size_t caller_content_length = 0;

  if (headers) {
    for (int i = 0; headers[i] != nullptr; ++i) {
      std::string h(headers[i]);
      if (h.rfind("User-Agent:", 0) == 0 || h.rfind("user-agent:", 0) == 0)
        has_user_agent = true;
      if (h.rfind("Accept:", 0) == 0 || h.rfind("accept:", 0) == 0)
        has_accept = true;
      if (h.rfind("Accept-Language:", 0) == 0 ||
          h.rfind("accept-language:", 0) == 0)
        has_accept_language = true;
      if (h.rfind("Accept-Charset:", 0) == 0 ||
          h.rfind("accept-charset:", 0) == 0)
        has_accept_charset = true;
      if (h.rfind("DNT:", 0) == 0 || h.rfind("dnt:", 0) == 0)
        has_dnt = true;
      if (h.rfind("Connection:", 0) == 0 || h.rfind("connection:", 0) == 0)
        has_connection = true;
      if (h.rfind("Content-Type:", 0) == 0 || h.rfind("content-type:", 0) == 0)
        has_content_type = true;
      if (h.rfind("Content-Length:", 0) == 0 ||
          h.rfind("content-length:", 0) == 0) {
        has_content_length = true;
        caller_content_length = std::strtoul(
            h.c_str() + kContentLengthPrefix.size(), nullptr, 10);
      }
      if (h.rfind("Cookie:", 0) == 0 || h.rfind("cookie:", 0) == 0)
        has_cookie = true;
      ctx->request_data += h + "\r\n";

      size_t colon = h.find(':');
      if (colon != std::string::npos) {
        std::string key = h.substr(0, colon);
        std::string val = h.substr(colon + 1);
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
          val.erase(val.begin());
        while (!val.empty() && (val.back() == '\r' || val.back() == '\n' ||
                                val.back() == ' ' || val.back() == '\t'))
          val.pop_back();
        ctx->request_headers.push_back({key, val});
      }
    }
  }

  if (!has_user_agent) {
    ctx->request_data += "User-Agent: ";
    ctx->request_data += kDefaultUserAgent;
    ctx->request_data += "\r\n";
    ctx->request_headers.push_back(
        {"user-agent", std::string(kDefaultUserAgent)});
  }

  if (!has_accept) {
    ctx->request_data += "Accept: ";
    ctx->request_data += kDefaultAccept;
    ctx->request_data += "\r\n";
    ctx->request_headers.push_back({"accept", std::string(kDefaultAccept)});
  }

  if (!has_accept_language) {
    const char* configured_lang = nsoption_charp(accept_language);
    std::string_view lang_val =
        (configured_lang != nullptr && configured_lang[0] != '\0')
            ? std::string_view(configured_lang)
            : kDefaultAcceptLanguage;
    ctx->request_data += "Accept-Language: ";
    ctx->request_data += lang_val;
    ctx->request_data += "\r\n";
    ctx->request_headers.push_back({"accept-language", std::string(lang_val)});
  }

  if (!has_accept_charset) {
    const char* configured_charset = nsoption_charp(accept_charset);
    if (configured_charset != nullptr && configured_charset[0] != '\0') {
      ctx->request_data += "Accept-Charset: ";
      ctx->request_data += configured_charset;
      ctx->request_data += "\r\n";
      ctx->request_headers.push_back(
          {"accept-charset", std::string(configured_charset)});
    }
  }

  if (!has_dnt && nsoption_bool(do_not_track)) {
    ctx->request_data += "DNT: 1\r\n";
    ctx->request_headers.push_back({"dnt", "1"});
  }

  if (ctx->use_proxy && !ctx->is_https && !ctx->proxy_auth_header.empty())
    ctx->request_data +=
        "Proxy-Authorization: " + ctx->proxy_auth_header + "\r\n";

  if (!has_cookie) {
    char* cookie_str = urldb_get_cookie(url, true);
    if (cookie_str != nullptr) {
      if (cookie_str[0] != '\0') {
        NSLOG(netsurf, INFO, "Sending Cookie for %s: %.160s",
              nsurl_access(url), cookie_str);
        ctx->request_data += "Cookie: ";
        ctx->request_data += cookie_str;
        ctx->request_data += "\r\n";
        ctx->request_headers.push_back({"cookie", std::string(cookie_str)});
      }
      free(cookie_str);
    }
  }

  if (!has_connection)
    ctx->request_data += "Connection: close\r\n";

  if (post_multipart != nullptr) {
    ctx->post_body.clear();
    for (const struct fetch_multipart_data* part = post_multipart;
         part != nullptr; part = part->next) {
      ctx->post_body += "--";
      ctx->post_body += kMultipartBoundary;
      ctx->post_body += "\r\n";
      const char* part_name = (part->name != nullptr) ? part->name : "";
      if (!part->file) {
        ctx->post_body += "Content-Disposition: form-data; name=\"";
        ctx->post_body += part_name;
        ctx->post_body += "\"\r\n\r\n";
        if (part->value != nullptr)
          ctx->post_body += part->value;
        ctx->post_body += "\r\n";
      } else {
        const char* primary_path =
            (part->value != nullptr && part->value[0] != '\0') ? part->value
                                                               : part->rawfile;
        std::string filename =
            (primary_path != nullptr) ? ExtractFilenameFromPath(primary_path) : "";
        std::string mime_type = GuessMimeTypeForFilename(filename);
        std::string file_contents;
        if (!ReadLocalFileBytes(primary_path, file_contents) &&
            part->rawfile != nullptr && part->rawfile != primary_path)
          ReadLocalFileBytes(part->rawfile, file_contents);

        ctx->post_body += "Content-Disposition: form-data; name=\"";
        ctx->post_body += part_name;
        ctx->post_body += "\"; filename=\"";
        ctx->post_body += filename;
        ctx->post_body += "\"\r\nContent-Type: ";
        ctx->post_body += mime_type;
        ctx->post_body += "\r\n\r\n";
        ctx->post_body.append(file_contents);
        ctx->post_body += "\r\n";
      }
    }
    ctx->post_body += "--";
    ctx->post_body += kMultipartBoundary;
    ctx->post_body += "--\r\n";

    std::string mp_content_type =
        "multipart/form-data; boundary=" + std::string(kMultipartBoundary);
    if (!has_content_type) {
      ctx->request_data += "Content-Type: " + mp_content_type + "\r\n";
      ctx->request_headers.push_back({"content-type", mp_content_type});
    }
    if (!has_content_length) {
      std::string len_str = std::to_string(ctx->post_body.size());
      ctx->request_data += "Content-Length: " + len_str + "\r\n";
      ctx->request_headers.push_back({"content-length", len_str});
    }
    RecordRequestHeaders(parent_fetch, ctx->request_data);
    ctx->request_data += "\r\n";
    ctx->request_data += ctx->post_body;
  } else if (post_urlenc) {
    // A caller-supplied Content-Length marks a binary-safe body that may
    // contain NUL bytes, so it takes precedence over strlen.
    size_t post_len =
        has_content_length ? caller_content_length : strlen(post_urlenc);
    ctx->post_body.assign(post_urlenc, post_len);
    if (!has_content_type) {
      ctx->request_data +=
          "Content-Type: application/x-www-form-urlencoded\r\n";
      ctx->request_headers.push_back(
          {"content-type", "application/x-www-form-urlencoded"});
    }
    if (!has_content_length) {
      std::string len_str = std::to_string(post_len);
      ctx->request_data += "Content-Length: " + len_str + "\r\n";
      ctx->request_headers.push_back({"content-length", len_str});
    }
    RecordRequestHeaders(parent_fetch, ctx->request_data);
    ctx->request_data += "\r\n";
    ctx->request_data += ctx->post_body;
  } else {
    RecordRequestHeaders(parent_fetch, ctx->request_data);
    ctx->request_data += "\r\n";
  }

  if (ctx->is_https)
    init_ssl_context(ctx);

  return ctx;
}

static bool http_fetch_start(void* handle) {
  auto ctx = (http_fetch_context*)handle;

  ctx->active = true;
  ctx->connecting_in_background = true;
  ctx->ref_count.fetch_add(1);
  {
    std::scoped_lock lock(active_fetches_mutex);
    active_http_fetches.push_back(ctx);
  }

  ::perception::Defer([ctx]() {
    struct FiberExitGuard {
      http_fetch_context* context;
      ~FiberExitGuard() {
        context->connecting_in_background = false;
        ReleaseContext(context);
      }
    };
    FiberExitGuard exit_guard{ctx};

    if (!ctx->active || ctx->free_deferred) {
      CloseContextSocket(ctx);
      return;
    }

    while (ctx->active && !ctx->free_deferred) {
      size_t count = active_socket_count.load();
      if (count < kMaxConcurrentSockets) {
        if (active_socket_count.compare_exchange_weak(count, count + 1)) {
          ctx->socket_counted = true;
          break;
        }
      } else {
        ::perception::SleepForDuration(kSocketSlotWaitInterval);
      }
    }

    if (!ctx->active || ctx->free_deferred) {
      CloseContextSocket(ctx);
      return;
    }

    const std::string& connect_host =
        ctx->use_proxy ? ctx->proxy_host : ctx->host;
    int connect_port = ctx->use_proxy ? ctx->proxy_port : ctx->port;

    for (int retry = 0; retry < 3; ++retry) {
      if (retry > 0) {
        ::perception::SleepForDuration(kConnectRetryDelay);
        if (!ctx->active || ctx->free_deferred) {
          CloseContextSocket(ctx);
          return;
        }
      }

      ctx->sock = ::perception::ConnectToHostAsFileDescriptor(
          connect_host, static_cast<uint16>(connect_port));
      if (!ctx->active || ctx->free_deferred) {
        CloseContextSocket(ctx);
        return;
      }

      if (ctx->sock >= 0)
        break;
    }

    if (ctx->sock < 0) {
      std::cout << "NetSurf HTTP Fetcher: Failed to connect to "
                << connect_host.c_str() << ":" << (int64)connect_port
                << std::endl;
      CloseContextSocket(ctx);
      ctx->connect_failed = true;
      return;
    }

    if (ctx->use_proxy && ctx->is_https) {
      std::string host_port = ctx->host + ":" + std::to_string(ctx->port);
      std::string connect_req = "CONNECT " + host_port + " HTTP/1.1\r\nHost: " +
                                host_port + "\r\n";
      if (!ctx->proxy_auth_header.empty())
        connect_req += "Proxy-Authorization: " + ctx->proxy_auth_header + "\r\n";
      connect_req += "\r\n";

      size_t total_sent = 0;
      while (total_sent < connect_req.size() && ctx->active &&
             !ctx->free_deferred) {
        ssize_t n = write(ctx->sock, connect_req.data() + total_sent,
                          connect_req.size() - total_sent);
        if (n < 0) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ::perception::SleepForDuration(kSocketSlotWaitInterval);
            continue;
          }
          CloseContextSocket(ctx);
          ctx->connect_failed = true;
          return;
        }
        total_sent += static_cast<size_t>(n);
      }

      std::string connect_resp;
      while (connect_resp.size() < kMaxConnectResponseBytes && ctx->active &&
             !ctx->free_deferred) {
        char ch = 0;
        ssize_t n = read(ctx->sock, &ch, 1);
        if (n < 0) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ::perception::SleepForDuration(kSocketSlotWaitInterval);
            continue;
          }
          CloseContextSocket(ctx);
          ctx->connect_failed = true;
          return;
        }
        if (n == 0)
          break;
        connect_resp.push_back(ch);
        if (connect_resp.find("\r\n\r\n") != std::string::npos ||
            connect_resp.find("\n\n") != std::string::npos)
          break;
      }

      if (!ctx->active || ctx->free_deferred) {
        CloseContextSocket(ctx);
        return;
      }

      bool tunnel_ok = false;
      size_t space = connect_resp.find(' ');
      if (space != std::string::npos && space + 4 <= connect_resp.size()) {
        std::string_view code_sv(connect_resp.data() + space + 1, 3);
        if (code_sv == "200")
          tunnel_ok = true;
      }
      if (!tunnel_ok) {
        CloseContextSocket(ctx);
        ctx->connect_failed = true;
        return;
      }
    }

    int flags = fcntl(ctx->sock, F_GETFL, 0);
    if (flags >= 0)
      fcntl(ctx->sock, F_SETFL, flags | O_NONBLOCK);
  });

  return true;
}

static void http_fetch_abort(void* handle) {
  auto ctx = (http_fetch_context*)handle;
  ctx->active = false;
  CloseContextSocket(ctx);
  RemoveFetchFromQueuesOnce(ctx);
  FreeParentFetchOnce(ctx);
}

static void http_fetch_free(void* handle) {
  auto ctx = (http_fetch_context*)handle;
  {
    std::scoped_lock lock(active_fetches_mutex);
    auto it =
        std::find(active_http_fetches.begin(), active_http_fetches.end(), ctx);
    if (it != active_http_fetches.end())
      active_http_fetches.erase(it);
  }
  ctx->free_deferred = true;
  ctx->active = false;
  ctx->freed = true;
  ctx->fetch_freed = true;
  RemoveFetchFromQueuesOnce(ctx);
  ctx->parent_fetch = nullptr;
  CloseContextSocket(ctx);
  ReleaseContext(ctx);
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

static std::string to_lower_ascii(std::string_view s) {
  std::string lower(s);
  for (char& c : lower) {
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
  }
  return lower;
}

static bool is_http_response_complete(const http_fetch_context* ctx) {
  if (ctx->socket_closed) {
    return true;
  }

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
  std::string lower_headers = to_lower_ascii(headers);

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
    if (lower_headers.find("location:") != std::string::npos) {
      return true;
    }
  }

  // 204 No Content and 304 Not Modified responses have no body.
  if (status_code == 204 || status_code == 304) {
    return true;
  }

  // Check for Content-Length (case-insensitive)
  size_t cl_pos = lower_headers.find("content-length:");
  if (cl_pos != std::string::npos) {
    size_t next_line = lower_headers.find("\n", cl_pos);
    std::string line = lower_headers.substr(cl_pos, next_line != std::string::npos
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

  // Check for Transfer-Encoding: chunked (case-insensitive)
  size_t te_pos = lower_headers.find("transfer-encoding:");
  if (te_pos != std::string::npos) {
    size_t next_line = lower_headers.find("\n", te_pos);
    std::string line = lower_headers.substr(te_pos, next_line != std::string::npos
                                                        ? (next_line - te_pos)
                                                        : std::string::npos);
    if (line.find("chunked") != std::string::npos) {
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
  CloseContextSocket(ctx);

  if (ctx->response_data.empty()) {
    if (ctx->is_https) {
      std::scoped_lock lock(active_fetches_mutex);
      tls_session_cache.erase(ctx->host);
    }
    RemoveFetchFromQueuesOnce(ctx);
    fetch_msg msg;
    msg.type = FETCH_ERROR;
    msg.data.error = "EmptyHttpResponse";
    fetch_send_callback(&msg, ctx->parent_fetch);
    FreeParentFetchOnce(ctx);
    return;
  }

  std::string& resp = ctx->response_data;
  size_t ddelim = resp.find("\r\n\r\n");
  if (ddelim == std::string::npos)
    ddelim = resp.find("\n\n");

  std::string headers = "";
  std::string body = resp;
  if (ddelim != std::string::npos) {
    headers = resp.substr(0, ddelim);
    size_t delim_len = (resp.compare(ddelim, 4, "\r\n\r\n") == 0) ? 4 : 2;
    body = resp.substr(ddelim + delim_len);
  }

  bool is_chunked = false;
  std::string lower_headers = to_lower_ascii(headers);
  size_t te_pos = lower_headers.find("transfer-encoding:");
  if (te_pos != std::string::npos) {
    size_t next_line = lower_headers.find("\n", te_pos);
    std::string line = lower_headers.substr(te_pos, next_line != std::string::npos
                                                        ? (next_line - te_pos)
                                                        : std::string::npos);
    if (line.find("chunked") != std::string::npos)
      is_chunked = true;
  }

  if (is_chunked) {
    std::string decoded = "";
    size_t pos = 0;
    while (pos < body.length()) {
      size_t next_line = body.find("\r\n", pos);
      if (next_line == std::string::npos) break;
      std::string hex_str = body.substr(pos, next_line - pos);
      size_t semi = hex_str.find(';');
      if (semi != std::string::npos)
        hex_str = hex_str.substr(0, semi);
      size_t chunk_size = ParseHex(hex_str);
      if (chunk_size == 0)
        break;
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

  NSLOG(netsurf, INFO,
        "HTTP response %s%s status=%ld chunked=%d raw=%zu body=%zu",
        ctx->host.c_str(), ctx->path_and_query.c_str(), status_code,
        (int)is_chunked, resp.length(), body.length());
  if (ctx->path_and_query.find("/search") != std::string::npos) {
    std::string stripped;
    size_t p = 0;
    while (p < body.size()) {
      size_t s = body.find("<script", p);
      if (s == std::string::npos) {
        stripped.append(body.substr(p));
        break;
      }
      stripped.append(body.substr(p, s - p));
      stripped.append("<SCRIPT_OMITTED/>");
      size_t e = body.find("</script>", s);
      if (e == std::string::npos) break;
      p = e + 9;
    }
    for (size_t i = 0; i < stripped.size(); i += 400) {
      NSLOG(netsurf, INFO, "HTML[%zu]: %.400s", i, stripped.c_str() + i);
    }
  }

  // Extract Set-Cookie headers before handling redirects or normal response delivery.
  {
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
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      size_t colon = line.find(':');
      if (colon != std::string::npos) {
        std::string key = to_lower_ascii(line.substr(0, colon));
        while (!key.empty() && (key.front() == ' ' || key.front() == '\t'))
          key.erase(key.begin());
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
          key.pop_back();
        if (key == "set-cookie") {
          std::string cookie_val = line.substr(colon + 1) + "\r\n";
          if (ctx->url != nullptr)
            urldb_set_cookie(cookie_val.c_str(), ctx->url, nullptr);
          else
            fetch_set_cookie(ctx->parent_fetch, cookie_val.c_str());
        }
      }
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
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
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

      RemoveFetchFromQueuesOnce(ctx);
      fetch_msg msg;
      msg.type = FETCH_REDIRECT;
      msg.data.redirect = redirect_url.c_str();
      fetch_send_callback(&msg, ctx->parent_fetch);
      FreeParentFetchOnce(ctx);
      return;
    }
  }

  fetch_set_http_code(ctx->parent_fetch, (http_response_code)status_code);

  size_t start = 0;
  while (start < headers.length()) {
    size_t end = headers.find("\r\n", start);
    size_t next_start = 0;
    if (end != std::string::npos) {
      next_start = end + 2;
    } else {
      end = headers.find("\n", start);
      if (end != std::string::npos)
        next_start = end + 1;
    }

    if (end == std::string::npos) {
      std::string h = headers.substr(start) + "\r\n";
      std::string lower_h = to_lower_ascii(h);
      if (is_chunked && lower_h.rfind("transfer-encoding:", 0) == 0) {
        // Skip forwarding Transfer-Encoding header when body was already chunk-decoded.
        break;
      }
      fetch_msg msg;
      msg.type = FETCH_HEADER;
      msg.data.header_or_data.buf = (const uint8_t*)h.data();
      msg.data.header_or_data.len = h.length();
      fetch_send_callback(&msg, ctx->parent_fetch);
      if (ctx->freed) return;
      break;
    }

    std::string h = headers.substr(start, next_start - start);
    std::string lower_h = to_lower_ascii(h);
    if (is_chunked && lower_h.rfind("transfer-encoding:", 0) == 0) {
      // Skip forwarding Transfer-Encoding header when body was already chunk-decoded.
      start = next_start;
      continue;
    }
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

  RemoveFetchFromQueuesOnce(ctx);

  fetch_msg msg;
  msg.type = FETCH_FINISHED;
  fetch_send_callback(&msg, ctx->parent_fetch);
  FreeParentFetchOnce(ctx);
}

static void run_ssl_engine(http_fetch_context* ctx) {
  for (;;) {
    if (ctx->freed || !ctx->active) return;
    unsigned int state = br_ssl_engine_current_state(&ctx->sc.eng);

    // Save session parameters and inspect negotiated ALPN protocol upon
    // completing the TLS handshake.
    if (state & (BR_SSL_SENDAPP | BR_SSL_RECVAPP)) {
      if (!ctx->tls_session_saved) {
        ctx->tls_session_saved = true;
        std::scoped_lock lock(active_fetches_mutex);
        br_ssl_engine_get_session_parameters(&ctx->sc.eng,
                                             &tls_session_cache[ctx->host]);
      }
      if (!ctx->alpn_checked) {
        ctx->alpn_checked = true;
        RecordTlsInfo(ctx->parent_fetch, FormatTlsInfo(&ctx->sc.eng));
        const char* selected_proto =
            br_ssl_engine_get_selected_protocol(&ctx->sc.eng);
        if (selected_proto != nullptr &&
            std::string_view(selected_proto) == kAlpnH2) {
          ctx->is_http2 = true;
          ctx->h2_session.Initialize();
          ctx->h2_stream_id = ctx->h2_session.OpenStream(
              ctx->method, "https", ctx->host, ctx->path_and_query,
              ctx->request_headers, ctx->post_body);
          NSLOG(netsurf, INFO,
                "ALPN negotiated HTTP/2 (h2) for %s%s (stream=%u)",
                ctx->host.c_str(), ctx->path_and_query.c_str(),
                ctx->h2_stream_id);
        }
      }
    }

    if (state & BR_SSL_CLOSED) {
      int err = br_ssl_engine_last_error(&ctx->sc.eng);
      if (err != BR_ERR_OK) {
        std::scoped_lock lock(active_fetches_mutex);
        tls_session_cache.erase(ctx->host);
      }
      if (ctx->is_http2 && ctx->response_data.empty()) {
        const ::perception::http::Http2StreamState* stream =
            ctx->h2_session.GetStream(ctx->h2_stream_id);
        if (stream != nullptr && stream->headers_received &&
            !stream->reset_by_peer)
          ctx->response_data =
              ctx->h2_session.FormatStreamAsHttp1Response(ctx->h2_stream_id);
      }
      if (err != BR_ERR_OK && !ctx->socket_closed &&
          ctx->response_data.empty()) {
        std::cout << "NetSurf SSL engine closed with error: " << (int64)err
                  << std::endl;
        ctx->failed = true;
        ctx->failure_reason = "SslHandshakeOrConnectionFailed";
        CloseContextSocket(ctx);
      } else {
        process_http_response(ctx);
      }
      return;
    }

    if (ctx->socket_closed && !(state & BR_SSL_RECVAPP)) {
      if (ctx->is_http2 && ctx->response_data.empty()) {
        const ::perception::http::Http2StreamState* stream =
            ctx->h2_session.GetStream(ctx->h2_stream_id);
        if (stream != nullptr && stream->headers_received &&
            !stream->reset_by_peer)
          ctx->response_data =
              ctx->h2_session.FormatStreamAsHttp1Response(ctx->h2_stream_id);
      }
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
          br_ssl_engine_sendrec_ack(&ctx->sc.eng, len);
          did_work = true;
        } else {
          ssize_t sent = write(ctx->sock, buf, len);
          if (sent < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
              if (errno == ECONNRESET || errno == EPIPE) {
                br_ssl_engine_sendrec_ack(&ctx->sc.eng, len);
                did_work = true;
              } else {
                std::cout << "NetSurf SSL: Socket write failed! errno = "
                          << (int64)errno << std::endl;
                {
                  std::scoped_lock lock(active_fetches_mutex);
                  tls_session_cache.erase(ctx->host);
                }
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
            {
              std::scoped_lock lock(active_fetches_mutex);
              tls_session_cache.erase(ctx->host);
            }
            ctx->failed = true;
            ctx->failure_reason = "SocketReadFailed";
            return;
          }
        } else if (recved == 0) {
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
      if (ctx->is_http2) {
        if (ctx->h2_session.HasPendingSendBytes()) {
          std::span<const uint8_t> pending = ctx->h2_session.PendingSendBytes();
          size_t to_send = std::min(len, pending.size());
          if (to_send > 0) {
            std::memcpy(buf, pending.data(), to_send);
            br_ssl_engine_sendapp_ack(&ctx->sc.eng, to_send);
            ctx->h2_session.ConsumePendingSendBytes(to_send);
            did_work = true;
            if (!ctx->h2_session.HasPendingSendBytes())
              br_ssl_engine_flush(&ctx->sc.eng, 0);
          }
        }
      } else if (ctx->sent_bytes < ctx->request_data.length()) {
        size_t to_send =
            std::min(len, ctx->request_data.length() - ctx->sent_bytes);
        if (to_send > 0) {
          std::memcpy(buf, ctx->request_data.data() + ctx->sent_bytes, to_send);
          br_ssl_engine_sendapp_ack(&ctx->sc.eng, to_send);
          ctx->sent_bytes += to_send;
          did_work = true;
          if (ctx->sent_bytes >= ctx->request_data.length())
            br_ssl_engine_flush(&ctx->sc.eng, 0);
        }
      }
    }

    // Read decrypted plaintext response data from SSL engine
    if (state & BR_SSL_RECVAPP) {
      size_t len;
      unsigned char* buf = br_ssl_engine_recvapp_buf(&ctx->sc.eng, &len);
      if (len > 0) {
        if (ctx->is_http2) {
          std::span<const uint8_t> chunk(buf, len);
          bool ok = ctx->h2_session.ReceiveBytes(chunk);
          br_ssl_engine_recvapp_ack(&ctx->sc.eng, len);
          did_work = true;

          if (!ok || ctx->h2_session.IsStreamFailed(ctx->h2_stream_id)) {
            ctx->failed = true;
            ctx->failure_reason = "Http2ProtocolOrStreamError";
            CloseContextSocket(ctx);
            return;
          }

          if (ctx->h2_session.IsStreamComplete(ctx->h2_stream_id)) {
            ctx->response_data =
                ctx->h2_session.FormatStreamAsHttp1Response(ctx->h2_stream_id);
            process_http_response(ctx);
            return;
          }
        } else {
          ctx->response_data.append((const char*)buf, len);
          br_ssl_engine_recvapp_ack(&ctx->sc.eng, len);
          did_work = true;

          if (is_http_response_complete(ctx)) {
            process_http_response(ctx);
            return;
          }
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
    for (auto ctx : active_http_fetches) {
      ctx->ref_count.fetch_add(1);
      fetches_to_process.push_back(ctx);
    }
  }

  for (auto ctx : fetches_to_process) {
    struct PollExitGuard {
      http_fetch_context* context;
      ~PollExitGuard() {
        context->in_poll = false;
        ReleaseContext(context);
      }
    };
    PollExitGuard guard{ctx};

    if (ctx->freed || !ctx->active || ctx->finished)
      continue;

    if (ctx->connecting_in_background)
      continue;

    ctx->in_poll = true;

    if (ctx->connect_failed && !ctx->finished) {
      ctx->finished = true;
      ctx->active = false;
      RemoveFetchFromQueuesOnce(ctx);
      fetch_msg msg;
      msg.type = FETCH_ERROR;
      msg.data.error = "Failed to connect to host";
      fetch_send_callback(&msg, ctx->parent_fetch);
      FreeParentFetchOnce(ctx);
      continue;
    }

    if (ctx->failed) {
      ctx->active = false;
      CloseContextSocket(ctx);
      RemoveFetchFromQueuesOnce(ctx);
      fetch_msg msg;
      msg.type = FETCH_ERROR;
      msg.data.error = ctx->failure_reason.c_str();
      fetch_send_callback(&msg, ctx->parent_fetch);
      FreeParentFetchOnce(ctx);
      continue;
    }

    if (ctx->is_https) {
      run_ssl_engine(ctx);
      if (ctx->failed && ctx->active) {
        ctx->active = false;
        CloseContextSocket(ctx);
        RemoveFetchFromQueuesOnce(ctx);
        fetch_msg msg;
        msg.type = FETCH_ERROR;
        msg.data.error = ctx->failure_reason.c_str();
        fetch_send_callback(&msg, ctx->parent_fetch);
        FreeParentFetchOnce(ctx);
      }
      continue;
    }

    if (ctx->sent_bytes < ctx->request_data.length()) {
      ssize_t sent =
          write(ctx->sock, ctx->request_data.data() + ctx->sent_bytes,
                ctx->request_data.length() - ctx->sent_bytes);
      if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
          continue;

        ctx->active = false;
        CloseContextSocket(ctx);
        RemoveFetchFromQueuesOnce(ctx);

        fetch_msg msg;
        msg.type = FETCH_ERROR;
        msg.data.error = "SocketWriteFailed";
        fetch_send_callback(&msg, ctx->parent_fetch);
        FreeParentFetchOnce(ctx);
        continue;
      }
      ctx->sent_bytes += sent;
    }

    if (ctx->sent_bytes >= ctx->request_data.length()) {
      char buf[kHttpReadBufferSize];
      bool read_more = true;
      while (read_more && ctx->active && !ctx->finished) {
        ssize_t recved = read(ctx->sock, buf, sizeof(buf));
        if (recved < 0) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            read_more = false;
            break;
          }
          ctx->active = false;
          CloseContextSocket(ctx);
          RemoveFetchFromQueuesOnce(ctx);

          fetch_msg msg;
          msg.type = FETCH_ERROR;
          msg.data.error = "SocketReadFailed";
          fetch_send_callback(&msg, ctx->parent_fetch);
          FreeParentFetchOnce(ctx);
          read_more = false;
          break;
        } else if (recved == 0) {
          ctx->socket_closed = true;
          process_http_response(ctx);
          read_more = false;
          break;
        } else {
          ctx->response_data.append(buf, recved);
          if (is_http_response_complete(ctx)) {
            process_http_response(ctx);
            read_more = false;
            break;
          }
          if (recved < (ssize_t)sizeof(buf)) {
            read_more = false;
            break;
          }
        }
      }
    }
  }
}

void RegisterPerceptionHttpFetcher() {
  InitializeNetworkLog();

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
              << (int64)error << std::endl;
  }
}

}  // namespace perception
}  // namespace netsurf
