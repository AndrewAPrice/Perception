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

#include "module/net.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perception/network/ip_address.h"
#include "perception/network/network_service.h"

namespace module {
namespace {

// Default maximum number of HTTP redirects followed by fetch().
constexpr long kDefaultMaxRedirects = 10L;

// Default User-Agent header sent by fetch() when none is specified.
constexpr const char* kDefaultUserAgent = "Perception-jsshell/1.0";

// Lowest HTTP status code considered successful for Response.ok.
constexpr long kHttpOkMin = 200;

// Upper bound (exclusive) of HTTP status codes considered successful.
constexpr long kHttpOkMax = 300;

// Number of bytes in an Ethernet MAC address.
constexpr size_t kMacAddressBytes = 6;

std::string ToLowerAscii(std::string_view s) {
  std::string out(s);
  for (char& c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string_view TrimAsciiWhitespace(std::string_view s) {
  while (!s.empty() &&
         (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' ||
          s.front() == '\n')) {
    s.remove_prefix(1);
  }
  while (!s.empty() &&
         (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' ||
          s.back() == '\n')) {
    s.remove_suffix(1);
  }
  return s;
}

std::string FormatMacAddress(const perception::devices::MacAddress& mac) {
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                mac.mac[0], mac.mac[1], mac.mac[2], mac.mac[3], mac.mac[4],
                mac.mac[5]);
  return std::string(buf);
}

bool IsZeroMac(const perception::devices::MacAddress& mac) {
  for (size_t i = 0; i < kMacAddressBytes; ++i) {
    if (mac.mac[i] != 0) return false;
  }
  return true;
}

JSValue MakeResolvedPromise(JSContext* ctx, JSValue val) {
  if (JS_IsException(val)) return val;
  JSValue resolving_funcs[2] = {JS_UNDEFINED, JS_UNDEFINED};
  JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
  if (JS_IsException(promise)) {
    JS_FreeValue(ctx, val);
    return promise;
  }
  JSValue ret = JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &val);
  JS_FreeValue(ctx, ret);
  JS_FreeValue(ctx, val);
  JS_FreeValue(ctx, resolving_funcs[0]);
  JS_FreeValue(ctx, resolving_funcs[1]);
  return promise;
}

JSValue MakeRejectedPromise(JSContext* ctx, const char* message) {
  JSValue resolving_funcs[2] = {JS_UNDEFINED, JS_UNDEFINED};
  JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
  if (JS_IsException(promise)) return promise;

  JSValue err = JS_NewError(ctx);
  JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, message));
  JSValue ret = JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, &err);
  JS_FreeValue(ctx, ret);
  JS_FreeValue(ctx, err);
  JS_FreeValue(ctx, resolving_funcs[0]);
  JS_FreeValue(ctx, resolving_funcs[1]);
  return promise;
}

const char* DefaultHttpStatusText(long code) {
  switch (code) {
    case 200:
      return "OK";
    case 201:
      return "Created";
    case 204:
      return "No Content";
    case 301:
      return "Moved Permanently";
    case 302:
      return "Found";
    case 304:
      return "Not Modified";
    case 400:
      return "Bad Request";
    case 401:
      return "Unauthorized";
    case 403:
      return "Forbidden";
    case 404:
      return "Not Found";
    case 500:
      return "Internal Server Error";
    case 502:
      return "Bad Gateway";
    case 503:
      return "Service Unavailable";
    default:
      return "";
  }
}

struct CurlResponseBuffer {
  std::string body;
  std::string status_text;
  std::vector<std::pair<std::string, std::string>> headers;
};

size_t OnCurlWriteData(char* ptr, size_t size, size_t nmemb, void* userdata) {
  size_t total = size * nmemb;
  auto* buf = static_cast<CurlResponseBuffer*>(userdata);
  if (buf != nullptr && ptr != nullptr && total > 0)
    buf->body.append(ptr, total);
  return total;
}

size_t OnCurlHeaderData(char* buffer, size_t size, size_t nitems,
                        void* userdata) {
  size_t total = size * nitems;
  auto* resp = static_cast<CurlResponseBuffer*>(userdata);
  if (resp == nullptr || buffer == nullptr || total == 0) return total;

  std::string_view line(buffer, total);
  line = TrimAsciiWhitespace(line);
  if (line.empty()) return total;

  if (line.starts_with("HTTP/")) {
    resp->headers.clear();
    resp->status_text.clear();
    size_t first_space = line.find(' ');
    if (first_space != std::string_view::npos) {
      size_t second_space = line.find(' ', first_space + 1);
      if (second_space != std::string_view::npos) {
        resp->status_text =
            std::string(TrimAsciiWhitespace(line.substr(second_space + 1)));
      }
    }
    return total;
  }

  size_t colon = line.find(':');
  if (colon != std::string_view::npos) {
    std::string key = ToLowerAscii(TrimAsciiWhitespace(line.substr(0, colon)));
    std::string val(TrimAsciiWhitespace(line.substr(colon + 1)));
    resp->headers.emplace_back(std::move(key), std::move(val));
  }
  return total;
}

// ---- Fetch Response helper methods ----

JSValue JsResponseText(JSContext* ctx, JSValueConst this_val, int /*argc*/,
                       JSValueConst* /*argv*/) {
  JSValue raw = JS_GetPropertyStr(ctx, this_val, "_bodyBytes");
  size_t size = 0;
  uint8_t* ptr = JS_GetArrayBuffer(ctx, &size, raw);
  JSValue str = (ptr != nullptr)
                    ? JS_NewStringLen(ctx, reinterpret_cast<const char*>(ptr),
                                      size)
                    : JS_NewString(ctx, "");
  JS_FreeValue(ctx, raw);
  return MakeResolvedPromise(ctx, str);
}

JSValue JsResponseJson(JSContext* ctx, JSValueConst this_val, int /*argc*/,
                       JSValueConst* /*argv*/) {
  JSValue raw = JS_GetPropertyStr(ctx, this_val, "_bodyBytes");
  size_t size = 0;
  uint8_t* ptr = JS_GetArrayBuffer(ctx, &size, raw);
  std::string text;
  if (ptr != nullptr && size > 0)
    text.assign(reinterpret_cast<const char*>(ptr), size);
  JS_FreeValue(ctx, raw);

  JSValue parsed = JS_ParseJSON(ctx, text.c_str(), text.size(), "<fetch.json>");
  if (JS_IsException(parsed)) {
    JSValue exc = JS_GetException(ctx);
    const char* msg = JS_ToCString(ctx, exc);
    JSValue rej =
        MakeRejectedPromise(ctx, msg != nullptr ? msg : "Invalid JSON");
    if (msg != nullptr) JS_FreeCString(ctx, msg);
    JS_FreeValue(ctx, exc);
    return rej;
  }
  return MakeResolvedPromise(ctx, parsed);
}

JSValue JsResponseArrayBuffer(JSContext* ctx, JSValueConst this_val,
                              int /*argc*/, JSValueConst* /*argv*/) {
  JSValue raw = JS_GetPropertyStr(ctx, this_val, "_bodyBytes");
  return MakeResolvedPromise(ctx, raw);
}

JSValue JsResponseBytes(JSContext* ctx, JSValueConst this_val, int /*argc*/,
                        JSValueConst* /*argv*/) {
  JSValue raw = JS_GetPropertyStr(ctx, this_val, "_bodyBytes");
  JSValue global_obj = JS_GetGlobalObject(ctx);
  JSValue u8_ctor = JS_GetPropertyStr(ctx, global_obj, "Uint8Array");
  JSValue arg = raw;
  JSValue u8 = JS_CallConstructor(ctx, u8_ctor, 1, &arg);
  JS_FreeValue(ctx, u8_ctor);
  JS_FreeValue(ctx, global_obj);
  JS_FreeValue(ctx, raw);
  return MakeResolvedPromise(ctx, u8);
}

JSValue JsHeadersGet(JSContext* ctx, JSValueConst this_val, int argc,
                     JSValueConst* argv) {
  if (argc < 1) return JS_NULL;
  const char* name = JS_ToCString(ctx, argv[0]);
  if (name == nullptr) return JS_EXCEPTION;
  std::string lower = ToLowerAscii(name);
  JS_FreeCString(ctx, name);

  JSValue val = JS_GetPropertyStr(ctx, this_val, lower.c_str());
  if (JS_IsUndefined(val)) {
    JS_FreeValue(ctx, val);
    return JS_NULL;
  }
  return val;
}

JSValue JsHeadersHas(JSContext* ctx, JSValueConst this_val, int argc,
                     JSValueConst* argv) {
  if (argc < 1) return JS_FALSE;
  const char* name = JS_ToCString(ctx, argv[0]);
  if (name == nullptr) return JS_EXCEPTION;
  std::string lower = ToLowerAscii(name);
  JS_FreeCString(ctx, name);

  JSValue val = JS_GetPropertyStr(ctx, this_val, lower.c_str());
  bool exists = !JS_IsUndefined(val) && !JS_IsNull(val);
  JS_FreeValue(ctx, val);
  return JS_NewBool(ctx, exists ? 1 : 0);
}

// ---- Global fetch(url, options?) ----

JSValue JsGlobalFetch(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                      JSValueConst* argv) {
  if (argc < 1)
    return MakeRejectedPromise(ctx, "fetch requires a URL argument");

  const char* url_cstr = JS_ToCString(ctx, argv[0]);
  if (url_cstr == nullptr) return JS_EXCEPTION;
  std::string url(url_cstr);
  JS_FreeCString(ctx, url_cstr);

  CURL* curl = curl_easy_init();
  if (curl == nullptr)
    return MakeRejectedPromise(ctx, "Failed to initialize libcurl handle");

  std::string method = "GET";
  std::string request_body;
  bool has_body = false;
  bool follow_redirects = true;
  long timeout_ms = 0;
  struct curl_slist* req_headers = nullptr;

  if (argc >= 2 && JS_IsObject(argv[1])) {
    JSValueConst opts = argv[1];

    JSValue method_val = JS_GetPropertyStr(ctx, opts, "method");
    if (JS_IsString(method_val)) {
      const char* m = JS_ToCString(ctx, method_val);
      if (m != nullptr) {
        method = m;
        for (char& c : method)
          c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        JS_FreeCString(ctx, m);
      }
    }
    JS_FreeValue(ctx, method_val);

    JSValue redir_val = JS_GetPropertyStr(ctx, opts, "redirect");
    if (JS_IsString(redir_val)) {
      const char* r = JS_ToCString(ctx, redir_val);
      if (r != nullptr) {
        if ( std::strcmp(r, "manual") == 0 || std::strcmp(r, "error") == 0)
          follow_redirects = false;
        JS_FreeCString(ctx, r);
      }
    } else if (JS_IsBool(redir_val)) {
      follow_redirects = (JS_ToBool(ctx, redir_val) != 0);
    }
    JS_FreeValue(ctx, redir_val);

    JSValue timeout_val = JS_GetPropertyStr(ctx, opts, "timeout");
    if (JS_IsNumber(timeout_val)) {
      int64_t t = 0;
      JS_ToInt64(ctx, &t, timeout_val);
      if (t > 0) timeout_ms = static_cast<long>(t);
    }
    JS_FreeValue(ctx, timeout_val);

    JSValue headers_val = JS_GetPropertyStr(ctx, opts, "headers");
    if (JS_IsObject(headers_val)) {
      JSPropertyEnum* tab = nullptr;
      uint32_t plen = 0;
      if (JS_GetOwnPropertyNames(ctx, &tab, &plen, headers_val,
                                 JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
        for (uint32_t i = 0; i < plen; ++i) {
          const char* k = JS_AtomToCString(ctx, tab[i].atom);
          if (k == nullptr) continue;
          JSValue v = JS_GetProperty(ctx, headers_val, tab[i].atom);
          const char* vc = JS_ToCString(ctx, v);
          if (vc != nullptr) {
            std::string hline = std::string(k) + ": " + vc;
            req_headers = curl_slist_append(req_headers, hline.c_str());
            JS_FreeCString(ctx, vc);
          }
          JS_FreeValue(ctx, v);
          JS_FreeCString(ctx, k);
        }
        JS_FreePropertyEnum(ctx, tab, plen);
      }
    }
    JS_FreeValue(ctx, headers_val);

    JSValue body_val = JS_GetPropertyStr(ctx, opts, "body");
    if (!JS_IsUndefined(body_val) && !JS_IsNull(body_val)) {
      has_body = true;
      size_t ab_size = 0;
      uint8_t* ab_ptr = JS_GetArrayBuffer(ctx, &ab_size, body_val);
      if (ab_ptr != nullptr) {
        request_body.assign(reinterpret_cast<const char*>(ab_ptr), ab_size);
      } else {
        JS_FreeValue(ctx, JS_GetException(ctx));
        size_t boff = 0;
        size_t blen = 0;
        size_t bpe = 0;
        JSValue typed_ab =
            JS_GetTypedArrayBuffer(ctx, body_val, &boff, &blen, &bpe);
        if (!JS_IsException(typed_ab)) {
          uint8_t* tptr = JS_GetArrayBuffer(ctx, &ab_size, typed_ab);
          if (tptr != nullptr && boff + blen <= ab_size) {
            request_body.assign(reinterpret_cast<const char*>(tptr + boff),
                                blen);
          }
          JS_FreeValue(ctx, typed_ab);
        } else {
          JS_FreeValue(ctx, JS_GetException(ctx));
          size_t slen = 0;
          const char* s = JS_ToCStringLen(ctx, &slen, body_val);
          if (s != nullptr) {
            request_body.assign(s, slen);
            JS_FreeCString(ctx, s);
          }
        }
      }
    }
    JS_FreeValue(ctx, body_val);
  }

  CurlResponseBuffer resp_buf;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, kDefaultUserAgent);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, follow_redirects ? 1L : 0L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, kDefaultMaxRedirects);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &OnCurlWriteData);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_buf);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &OnCurlHeaderData);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp_buf);

  if (timeout_ms > 0)
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
  if (req_headers != nullptr)
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, req_headers);

  if (method == "HEAD") {
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
  } else if (method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
  } else if (method != "GET") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
  }

  if (has_body) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(request_body.size()));
  }

  CURLcode res = curl_easy_perform(curl);
  if (req_headers != nullptr) curl_slist_free_all(req_headers);

  if (res != CURLE_OK) {
    std::string err_msg =
        std::string("fetch failed: ") + curl_easy_strerror(res);
    curl_easy_cleanup(curl);
    return MakeRejectedPromise(ctx, err_msg.c_str());
  }

  long status_code = 0;
  long redirect_count = 0;
  char* effective_url = nullptr;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
  curl_easy_getinfo(curl, CURLINFO_REDIRECT_COUNT, &redirect_count);
  curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective_url);
  std::string final_url = (effective_url != nullptr) ? effective_url : url;
  curl_easy_cleanup(curl);

  if (resp_buf.status_text.empty())
    resp_buf.status_text = DefaultHttpStatusText(status_code);

  JSValue resp_obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, resp_obj, "status",
                    JS_NewInt32(ctx, static_cast<int32_t>(status_code)));
  JS_SetPropertyStr(ctx, resp_obj, "statusText",
                    JS_NewString(ctx, resp_buf.status_text.c_str()));
  JS_SetPropertyStr(
      ctx, resp_obj, "ok",
      JS_NewBool(ctx,
                 (status_code >= kHttpOkMin && status_code < kHttpOkMax) ? 1
                                                                         : 0));
  JS_SetPropertyStr(ctx, resp_obj, "redirected",
                    JS_NewBool(ctx, redirect_count > 0 ? 1 : 0));
  JS_SetPropertyStr(ctx, resp_obj, "url",
                    JS_NewString(ctx, final_url.c_str()));

  JSValue headers_obj = JS_NewObject(ctx);
  for (const auto& kv : resp_buf.headers) {
    JS_SetPropertyStr(ctx, headers_obj, kv.first.c_str(),
                      JS_NewString(ctx, kv.second.c_str()));
  }
  JS_SetPropertyStr(ctx, headers_obj, "get",
                    JS_NewCFunction(ctx, &JsHeadersGet, "get", 1));
  JS_SetPropertyStr(ctx, headers_obj, "has",
                    JS_NewCFunction(ctx, &JsHeadersHas, "has", 1));
  JS_SetPropertyStr(ctx, resp_obj, "headers", headers_obj);

  const uint8_t* raw_ptr =
      reinterpret_cast<const uint8_t*>(resp_buf.body.data());
  JSValue body_ab = JS_NewArrayBufferCopy(ctx, raw_ptr, resp_buf.body.size());
  JS_SetPropertyStr(ctx, resp_obj, "_bodyBytes", body_ab);

  JS_SetPropertyStr(ctx, resp_obj, "text",
                    JS_NewCFunction(ctx, &JsResponseText, "text", 0));
  JS_SetPropertyStr(ctx, resp_obj, "json",
                    JS_NewCFunction(ctx, &JsResponseJson, "json", 0));
  JS_SetPropertyStr(ctx, resp_obj, "bytes",
                    JS_NewCFunction(ctx, &JsResponseBytes, "bytes", 0));
  JS_SetPropertyStr(
      ctx, resp_obj, "arrayBuffer",
      JS_NewCFunction(ctx, &JsResponseArrayBuffer, "arrayBuffer", 0));

  return MakeResolvedPromise(ctx, resp_obj);
}

// ---- net.* functions ----

JSValue JsNetInterfaces(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/,
                        JSValueConst* /*argv*/) {
  auto status_or =
      perception::GetService<perception::network::NetworkService>()
          .GetInterfaces();
  if (!status_or.Ok())
    return MakeRejectedPromise(ctx, "Failed to query network interfaces");

  JSValue arr = JS_NewArray(ctx);
  for (size_t i = 0; i < status_or->interfaces.size(); ++i) {
    const auto& iface = status_or->interfaces[i];
    JSValue obj = JS_NewObject(ctx);

    std::string iface_name = "eth" + std::to_string(i);
    if (IsZeroMac(iface.mac)) iface_name = "lo";
    JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, iface_name.c_str()));
    JS_SetPropertyStr(ctx, obj, "mac",
                      JS_NewString(ctx, FormatMacAddress(iface.mac).c_str()));
    JS_SetPropertyStr(ctx, obj, "mtu", JS_NewUint32(ctx, iface.mtu));

    JSValue addr_arr = JS_NewArray(ctx);
    JSValue ipv4_arr = JS_NewArray(ctx);
    JSValue ipv6_arr = JS_NewArray(ctx);
    uint32_t v4_idx = 0;
    uint32_t v6_idx = 0;

    for (size_t a = 0; a < iface.addresses.size(); ++a) {
      const auto& addr_info = iface.addresses[a];
      std::string ip_str = addr_info.address.ToString();

      JSValue aobj = JS_NewObject(ctx);
      JS_SetPropertyStr(ctx, aobj, "address",
                        JS_NewString(ctx, ip_str.c_str()));
      JS_SetPropertyStr(
          ctx, aobj, "family",
          JS_NewString(ctx, addr_info.address.IsV6() ? "IPv6" : "IPv4"));
      JS_SetPropertyStr(ctx, aobj, "prefixLength",
                        JS_NewUint32(ctx, addr_info.prefix_length));
      JS_SetPropertyStr(ctx, aobj, "state",
                        JS_NewUint32(ctx, addr_info.state));
      JS_SetPropertyStr(ctx, aobj, "origin",
                        JS_NewUint32(ctx, addr_info.origin));
      JS_SetPropertyUint32(ctx, addr_arr, static_cast<uint32_t>(a), aobj);

      if (addr_info.address.IsV4()) {
        JS_SetPropertyUint32(ctx, ipv4_arr, v4_idx++,
                             JS_NewString(ctx, ip_str.c_str()));
      } else if (addr_info.address.IsV6()) {
        JS_SetPropertyUint32(ctx, ipv6_arr, v6_idx++,
                             JS_NewString(ctx, ip_str.c_str()));
      }
    }
    JS_SetPropertyStr(ctx, obj, "addresses", addr_arr);
    JS_SetPropertyStr(ctx, obj, "ipv4", ipv4_arr);
    JS_SetPropertyStr(ctx, obj, "ipv6", ipv6_arr);

    JSValue routers_arr = JS_NewArray(ctx);
    for (size_t r = 0; r < iface.default_routers.size(); ++r) {
      JS_SetPropertyUint32(
          ctx, routers_arr, static_cast<uint32_t>(r),
          JS_NewString(ctx, iface.default_routers[r].ToString().c_str()));
    }
    JS_SetPropertyStr(ctx, obj, "routers", JS_DupValue(ctx, routers_arr));
    JS_SetPropertyStr(ctx, obj, "defaultRouters", routers_arr);

    JSValue dns_arr = JS_NewArray(ctx);
    for (size_t d = 0; d < iface.dns_servers.size(); ++d) {
      JS_SetPropertyUint32(
          ctx, dns_arr, static_cast<uint32_t>(d),
          JS_NewString(ctx, iface.dns_servers[d].ToString().c_str()));
    }
    JS_SetPropertyStr(ctx, obj, "dns", JS_DupValue(ctx, dns_arr));
    JS_SetPropertyStr(ctx, obj, "dnsServers", dns_arr);

    JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i), obj);
  }
  return MakeResolvedPromise(ctx, arr);
}

JSValue JsNetResolve(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                     JSValueConst* argv) {
  if (argc < 1)
    return MakeRejectedPromise(ctx, "net.resolve requires a hostname argument");

  const char* host_cstr = JS_ToCString(ctx, argv[0]);
  if (host_cstr == nullptr) return JS_EXCEPTION;
  perception::network::ResolveHostRequest req;
  req.host = host_cstr;
  JS_FreeCString(ctx, host_cstr);

  req.family = perception::network::IpAddressFamily::Unspecified;
  if (argc >= 2) {
    if (JS_IsNumber(argv[1])) {
      int32_t fam = 0;
      JS_ToInt32(ctx, &fam, argv[1]);
      if (fam == 4) req.family = perception::network::IpAddressFamily::V4;
      if (fam == 6) req.family = perception::network::IpAddressFamily::V6;
    } else if (JS_IsString(argv[1])) {
      const char* fstr = JS_ToCString(ctx, argv[1]);
      if (fstr != nullptr) {
        std::string lower = ToLowerAscii(fstr);
        if (lower == "ipv4" || lower == "v4" || lower == "4")
          req.family = perception::network::IpAddressFamily::V4;
        if (lower == "ipv6" || lower == "v6" || lower == "6")
          req.family = perception::network::IpAddressFamily::V6;
        JS_FreeCString(ctx, fstr);
      }
    }
  }

  auto status_or =
      perception::GetService<perception::network::NetworkService>().ResolveHost(
          req);
  if (!status_or.Ok()) {
    std::string msg = "Failed to resolve host: " + req.host;
    return MakeRejectedPromise(ctx, msg.c_str());
  }

  JSValue arr = JS_NewArray(ctx);
  for (size_t i = 0; i < status_or->addresses.size(); ++i) {
    JS_SetPropertyUint32(
        ctx, arr, static_cast<uint32_t>(i),
        JS_NewString(ctx, status_or->addresses[i].ToString().c_str()));
  }
  return MakeResolvedPromise(ctx, arr);
}

}  // namespace

void RegisterNetModule(JSContext* ctx, JsEngine& /*engine*/) {
  JSValue global_obj = JS_GetGlobalObject(ctx);

  JSValue net_obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, net_obj, "interfaces",
                    JS_NewCFunction(ctx, &JsNetInterfaces, "interfaces", 0));
  JS_SetPropertyStr(ctx, net_obj, "resolve",
                    JS_NewCFunction(ctx, &JsNetResolve, "resolve", 2));
  JS_SetPropertyStr(ctx, net_obj, "fetch",
                    JS_NewCFunction(ctx, &JsGlobalFetch, "fetch", 2));
  JS_SetPropertyStr(ctx, global_obj, "net", net_obj);

  JS_SetPropertyStr(ctx, global_obj, "fetch",
                    JS_NewCFunction(ctx, &JsGlobalFetch, "fetch", 2));

  JS_FreeValue(ctx, global_obj);
}

}  // namespace module
