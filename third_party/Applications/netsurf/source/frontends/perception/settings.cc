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

#include "settings.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
#include "netsurf/browser_window.h"
#include "netsurf/plot_style.h"
#include "utils/nsoption.h"
#include "utils/url.h"
}

#include "misc.h"
#include "perception/fibers.h"
#include "perception/registry.h"
#include "tabs.h"
#include "window.h"

namespace netsurf {
namespace perception {
namespace {

// Default homepage URL when none is configured in the registry.
constexpr std::string_view kDefaultHomepageUrl = "https://www.google.com";

// Default search engine URL template used when searching from the address bar.
constexpr std::string_view kDefaultSearchUrlTemplate =
    "https://www.google.com/search?q=%s";

// Placeholder token in search URL templates replaced with the escaped query.
constexpr std::string_view kSearchQueryPlaceholder = "%s";

// Default value for showing URL autocomplete suggestions.
constexpr bool kDefaultUrlSuggestion = true;

// Default value for opening target="_blank" links in a new tab.
constexpr bool kDefaultTargetBlank = true;

// Default value for opening middle-clicked links in a new tab.
constexpr bool kDefaultButton2Tab = true;

// Default value for switching immediately to newly opened tabs.
constexpr bool kDefaultForegroundNew = false;

// Default value for preferring dark mode on web pages.
constexpr bool kDefaultPreferDarkMode = false;

// Default page zoom percentage.
constexpr int kDefaultScalePercent = 100;

// Minimum allowed page zoom percentage.
constexpr int kMinScalePercent = 50;

// Maximum allowed page zoom percentage.
constexpr int kMaxScalePercent = 300;

// Default value for allowing author-level CSS stylesheets.
constexpr bool kDefaultAuthorLevelCss = true;

// Default 1-based font family option index (1 = Sans-serif).
constexpr int kDefaultFontFamilyOption = 1;

// Default base font size in points.
constexpr int kDefaultFontSizePt = 12;

// Default minimum font size in points.
constexpr int kDefaultFontMinSizePt = 8;

// Default treeview font size in points.
constexpr int kDefaultTreeviewFontSizePt = 11;

// Conversion factor from points to NetSurf decipoints.
constexpr int kDecipointsPerPoint = 10;

// Default value for enabling JavaScript execution.
constexpr bool kDefaultEnableJavascript = true;

// Default JavaScript execution timeout in seconds.
constexpr int kDefaultScriptTimeoutSeconds = 10;

// Default value for loading foreground images.
constexpr bool kDefaultForegroundImages = true;

// Default value for loading CSS background images.
constexpr bool kDefaultBackgroundImages = true;

// Default value for playing animated images.
constexpr bool kDefaultAnimateImages = true;

// Default value for incremental page reflow during loading.
constexpr bool kDefaultIncrementalReflow = true;

// Default minimum reflow period in milliseconds.
constexpr int kDefaultMinReflowPeriodMs = 25;

// Conversion factor from milliseconds to NetSurf centiseconds.
constexpr int kMillisecondsPerCentisecond = 10;

// Default value for blocking advertisements.
constexpr bool kDefaultBlockAdvertisements = false;

// Default value for sending the Do Not Track header.
constexpr bool kDefaultDoNotTrack = false;

// Default value for sending the HTTP Referer header.
constexpr bool kDefaultSendReferer = true;

// Default value for displaying decoded international domain names.
constexpr bool kDefaultDisplayDecodedIdn = false;

// Default number of days to retain browsing history entries.
constexpr int kDefaultExpireUrlDays = 28;

// Default value for persisting browsing history, cookies, and bookmarks.
constexpr bool kDefaultPersistStorage = true;

// Default HTTP Accept-Language header value.
constexpr std::string_view kDefaultAcceptLanguage = "en-US,en;q=0.5";

// Default HTTP Accept-Charset header value.
constexpr std::string_view kDefaultAcceptCharset = "UTF-8";

// Default maximum simultaneous active fetchers.
constexpr int kDefaultMaxFetchers = 24;

// Default maximum simultaneous fetchers per host.
constexpr int kDefaultMaxFetchersPerHost = 5;

// Default maximum number of retried fetches.
constexpr int kDefaultMaxRetriedFetches = 1;

// Default maximum number of redirects per fetch.
constexpr int kDefaultFetchRedirectLimit = 10;

// Default in-memory cache size in megabytes.
constexpr int kDefaultMemoryCacheSizeMb = 12;

// Default on-disk cache size in megabytes.
constexpr int kDefaultDiscCacheSizeMb = 128;

// Number of bytes in one megabyte.
constexpr int kBytesPerMegabyte = 1024 * 1024;

// Default disk cache expiry age in days.
constexpr int kDefaultDiscCacheAgeDays = 28;

// Default value for enabling an HTTP proxy.
constexpr bool kDefaultHttpProxy = false;

// Default HTTP proxy port number.
constexpr int kDefaultHttpProxyPort = 8080;

// Default HTTP proxy authentication mode (OPTION_HTTP_PROXY_AUTH_NONE).
constexpr int kDefaultHttpProxyAuth = 0;

// Default comma-separated list of hosts that bypass the HTTP proxy.
constexpr std::string_view kDefaultHttpProxyNoProxy = "localhost,127.0.0.1";

// Registry key for the homepage URL setting.
constexpr std::string_view kKeyHomepage = "homepage";

// Registry key for the search engine URL setting.
constexpr std::string_view kKeySearchUrl = "search_url";

// Registry key for the URL autocomplete suggestion setting.
constexpr std::string_view kKeyUrlSuggestion = "url_suggestion";

// Registry key for opening target="_blank" links in a new tab.
constexpr std::string_view kKeyTargetBlank = "target_blank";

// Registry key for middle-click opening links in a new tab.
constexpr std::string_view kKeyButton2Tab = "button_2_tab";

// Registry key for switching immediately to newly opened tabs.
constexpr std::string_view kKeyForegroundNew = "foreground_new";

// Registry key for preferring dark mode.
constexpr std::string_view kKeyPreferDarkMode = "prefer_dark_mode";

// Registry key for default page zoom scale percentage.
constexpr std::string_view kKeyScale = "scale";

// Registry key for allowing author-level CSS stylesheets.
constexpr std::string_view kKeyAuthorLevelCss = "author_level_css";

// Registry key for the default font family setting.
constexpr std::string_view kKeyFontDefault = "font_default";

// Registry key for the default font size setting in points.
constexpr std::string_view kKeyFontSize = "font_size";

// Registry key for the minimum font size setting in points.
constexpr std::string_view kKeyFontMinSize = "font_min_size";

// Registry key for the treeview font size setting in points.
constexpr std::string_view kKeyTreeviewFontSize = "treeview_font_size";

// Registry key for the JavaScript toggle setting.
constexpr std::string_view kKeyEnableJavascript = "enable_javascript";

// Registry key for the JavaScript execution timeout setting.
constexpr std::string_view kKeyScriptTimeout = "script_timeout";

// Legacy registry key for loading both foreground and background images.
constexpr std::string_view kKeyLoadImages = "load_images";

// Registry key for loading foreground images.
constexpr std::string_view kKeyForegroundImages = "foreground_images";

// Registry key for loading background images.
constexpr std::string_view kKeyBackgroundImages = "background_images";

// Registry key for animating images.
constexpr std::string_view kKeyAnimateImages = "animate_images";

// Registry key for incremental page reflow.
constexpr std::string_view kKeyIncrementalReflow = "incremental_reflow";

// Registry key for minimum reflow period in milliseconds.
constexpr std::string_view kKeyMinReflowPeriod = "min_reflow_period";

// Registry key for advertisement blocking.
constexpr std::string_view kKeyBlockAdvertisements = "block_advertisements";

// Registry key for the Do Not Track header setting.
constexpr std::string_view kKeyDoNotTrack = "do_not_track";

// Registry key for sending the HTTP Referer header.
constexpr std::string_view kKeySendReferer = "send_referer";

// Registry key for displaying decoded international domain names.
constexpr std::string_view kKeyDisplayDecodedIdn = "display_decoded_idn";

// Registry key for URL history retention period in days.
constexpr std::string_view kKeyExpireUrl = "expire_url";

// Registry key for persisting cookies, history, and bookmarks to disk.
constexpr std::string_view kKeyPersistStorage = "persist_storage";

// Registry key for the Accept-Language header setting.
constexpr std::string_view kKeyAcceptLanguage = "accept_language";

// Registry key for the Accept-Charset header setting.
constexpr std::string_view kKeyAcceptCharset = "accept_charset";

// Registry key for maximum simultaneous fetchers.
constexpr std::string_view kKeyMaxFetchers = "max_fetchers";

// Registry key for maximum fetchers per host.
constexpr std::string_view kKeyMaxFetchersPerHost = "max_fetchers_per_host";

// Registry key for maximum retried fetches.
constexpr std::string_view kKeyMaxRetriedFetches = "max_retried_fetches";

// Registry key for fetch redirect limit.
constexpr std::string_view kKeyFetchRedirectLimit = "fetch_redirect_limit";

// Registry key for memory cache size in megabytes.
constexpr std::string_view kKeyMemoryCacheSize = "memory_cache_size";

// Registry key for disk cache size in megabytes.
constexpr std::string_view kKeyDiscCacheSize = "disc_cache_size";

// Registry key for disk cache expiry age in days.
constexpr std::string_view kKeyDiscCacheAge = "disc_cache_age";

// Registry key for enabling the HTTP proxy.
constexpr std::string_view kKeyHttpProxy = "http_proxy";

// Registry key for the HTTP proxy hostname.
constexpr std::string_view kKeyHttpProxyHost = "http_proxy_host";

// Registry key for the HTTP proxy port.
constexpr std::string_view kKeyHttpProxyPort = "http_proxy_port";

// Registry key for the HTTP proxy authentication method.
constexpr std::string_view kKeyHttpProxyAuth = "http_proxy_auth";

// Registry key for the HTTP proxy username.
constexpr std::string_view kKeyHttpProxyAuthUser = "http_proxy_auth_user";

// Registry key for the HTTP proxy password.
constexpr std::string_view kKeyHttpProxyAuthPass = "http_proxy_auth_pass";

// Registry key for the HTTP proxy bypass list.
constexpr std::string_view kKeyHttpProxyNoProxy = "http_proxy_noproxy";

// All non-color registry keys watched for live updates.
constexpr std::string_view kWatchedSettingKeys[] = {
    kKeyHomepage,           kKeySearchUrl,           kKeyUrlSuggestion,
    kKeyTargetBlank,        kKeyButton2Tab,          kKeyForegroundNew,
    kKeyPreferDarkMode,     kKeyScale,               kKeyAuthorLevelCss,
    kKeyFontDefault,        kKeyFontSize,            kKeyFontMinSize,
    kKeyTreeviewFontSize,   kKeyEnableJavascript,    kKeyScriptTimeout,
    kKeyLoadImages,         kKeyForegroundImages,    kKeyBackgroundImages,
    kKeyAnimateImages,      kKeyIncrementalReflow,   kKeyMinReflowPeriod,
    kKeyBlockAdvertisements, kKeyDoNotTrack,         kKeySendReferer,
    kKeyDisplayDecodedIdn,  kKeyExpireUrl,           kKeyPersistStorage,
    kKeyAcceptLanguage,     kKeyAcceptCharset,       kKeyMaxFetchers,
    kKeyMaxFetchersPerHost, kKeyMaxRetriedFetches,   kKeyFetchRedirectLimit,
    kKeyMemoryCacheSize,    kKeyDiscCacheSize,       kKeyDiscCacheAge,
    kKeyHttpProxy,          kKeyHttpProxyHost,       kKeyHttpProxyPort,
    kKeyHttpProxyAuth,      kKeyHttpProxyAuthUser,   kKeyHttpProxyAuthPass,
    kKeyHttpProxyNoProxy,
};

// Definition of a system color mapping between registry keys and NetSurf options.
struct SystemColorConfig {
  const char* key;
  enum nsoption_e nsc;
  colour default_colour;
};

// System color settings and their light-mode default colors.
constexpr SystemColorConfig kSystemColors[] = {
    {"sys_colour_AccentColor", NSOPTION_sys_colour_AccentColor, 0x00e48435},
    {"sys_colour_AccentColorText", NSOPTION_sys_colour_AccentColorText,
     0x00ffffff},
    {"sys_colour_ActiveText", NSOPTION_sys_colour_ActiveText, 0x000000ee},
    {"sys_colour_ButtonBorder", NSOPTION_sys_colour_ButtonBorder, 0x00aaaaaa},
    {"sys_colour_ButtonFace", NSOPTION_sys_colour_ButtonFace, 0x00dddddd},
    {"sys_colour_ButtonText", NSOPTION_sys_colour_ButtonText, 0x00000000},
    {"sys_colour_Canvas", NSOPTION_sys_colour_Canvas, 0x00ffffff},
    {"sys_colour_CanvasText", NSOPTION_sys_colour_CanvasText, 0x00000000},
    {"sys_colour_Field", NSOPTION_sys_colour_Field, 0x00ffffff},
    {"sys_colour_FieldText", NSOPTION_sys_colour_FieldText, 0x00000000},
    {"sys_colour_GrayText", NSOPTION_sys_colour_GrayText, 0x00777777},
    {"sys_colour_Highlight", NSOPTION_sys_colour_Highlight, 0x00e48435},
    {"sys_colour_HighlightText", NSOPTION_sys_colour_HighlightText, 0x00ffffff},
    {"sys_colour_LinkText", NSOPTION_sys_colour_LinkText, 0x00ee0000},
    {"sys_colour_Mark", NSOPTION_sys_colour_Mark, 0x0000ffff},
    {"sys_colour_MarkText", NSOPTION_sys_colour_MarkText, 0x00000000},
    {"sys_colour_SelectedItem", NSOPTION_sys_colour_SelectedItem, 0x00e48435},
    {"sys_colour_SelectedItemText", NSOPTION_sys_colour_SelectedItemText,
     0x00ffffff},
    {"sys_colour_VisitedText", NSOPTION_sys_colour_VisitedText, 0x008b1a55},
};

// Converts Perception ARGB 0xAARRGGBB to NetSurf 0x00BBGGRR.
constexpr colour ArgbToNetSurfColour(uint32 argb) {
  return static_cast<colour>(((argb & 0x000000FF) << 16) |
                             (argb & 0x0000FF00) |
                             ((argb & 0x00FF0000) >> 16));
}

// Extracts a 32-bit color integer from a registry Value.
std::optional<uint32> GetColorFromValue(
    const ::perception::serialization::Value& val) {
  if (auto color = val.ColorRGBValue())
    return *color;
  if (auto integer = val.IntegerValue())
    return static_cast<uint32>(*integer);
  return std::nullopt;
}

// Reads a boolean setting from the registry if present.
std::optional<bool> ReadBool(std::string_view key) {
  auto val = ::perception::GetRegistryValue(key);
  if (val.Ok() && val->BoolValue())
    return *val->BoolValue();
  return std::nullopt;
}

// Reads a boolean setting from the registry or returns the provided default.
bool ReadBoolOrDefault(std::string_view key, bool default_value) {
  return ReadBool(key).value_or(default_value);
}

// Reads an integer or slider setting from the registry, checking both integer
// and floating-point representations.
std::optional<int> ReadInt(std::string_view key) {
  auto val = ::perception::GetRegistryValue(key);
  if (!val.Ok())
    return std::nullopt;
  if (auto int_val = val->IntegerValue())
    return static_cast<int>(*int_val);
  if (auto float_val = val->FloatValue())
    return static_cast<int>(std::lround(*float_val));
  return std::nullopt;
}

// Reads an integer or slider setting from the registry or returns the default.
int ReadIntOrDefault(std::string_view key, int default_value) {
  return ReadInt(key).value_or(default_value);
}

// Reads a string setting from the registry if present.
std::optional<std::string> ReadString(std::string_view key) {
  auto val = ::perception::GetRegistryValue(key);
  if (val.Ok() && val->StringValue())
    return std::string(*val->StringValue());
  return std::nullopt;
}

// Reads a string setting from the registry or returns the provided default.
std::string ReadStringOrDefault(std::string_view key,
                                std::string_view default_value) {
  if (auto str = ReadString(key))
    return *str;
  return std::string(default_value);
}

// Invalidates and schedules reformat for all open browser tabs.
void InvalidateAllTabs() {
  for (auto* gw : GetOpenTabs()) {
    if (gw && gw->GetBrowserWindow()) {
      browser_window_schedule_reformat(gw->GetBrowserWindow());
      if (gw->GetContentNode())
        gw->GetContentNode()->Invalidate();
    }
  }
}

// Loads a single color setting from the registry into NetSurf options.
void LoadColorSetting(const SystemColorConfig& config) {
  auto val = ::perception::GetRegistryValue(config.key);
  if (val.Ok()) {
    if (auto color = GetColorFromValue(*val)) {
      nsoptions[config.nsc].value.c = ArgbToNetSurfColour(*color);
      return;
    }
  }
  nsoptions[config.nsc].value.c = config.default_colour;
}

// Active registry listener tokens to keep callbacks alive.
std::vector<::perception::RegistryListenerToken> listener_tokens;

// Current search engine URL template (may contain %s).
std::string search_url_template(kDefaultSearchUrlTemplate);

// Current search engine URL prefix (with trailing %s removed for legacy callers).
std::string search_url_prefix;

// Whether persistent storage of cookies, history, and bookmarks is enabled.
bool persist_storage_enabled = kDefaultPersistStorage;

// Last applied default page scale percentage.
int last_applied_scale = kDefaultScalePercent;

// Whether a deferred settings refresh is currently queued.
bool settings_refresh_deferred = false;

void UpdateSearchUrls(std::string_view raw_template) {
  search_url_template = raw_template.empty()
                            ? std::string(kDefaultSearchUrlTemplate)
                            : std::string(raw_template);
  search_url_prefix = search_url_template;
  size_t pos = search_url_prefix.find(kSearchQueryPlaceholder);
  if (pos != std::string::npos)
    search_url_prefix.erase(pos, kSearchQueryPlaceholder.size());
}

// Applies all settings from the registry into NetSurf's option table.
void ApplySettings(bool is_initial_load) {
  std::string homepage = ReadStringOrDefault(kKeyHomepage, kDefaultHomepageUrl);
  if (homepage.empty())
    homepage = std::string(kDefaultHomepageUrl);
  nsoption_set_charp(homepage_url, strdup(homepage.c_str()));
  if (is_initial_load)
    SetInitialUrl(homepage.c_str());

  UpdateSearchUrls(
      ReadStringOrDefault(kKeySearchUrl, kDefaultSearchUrlTemplate));

  nsoption_set_bool(url_suggestion,
                    ReadBoolOrDefault(kKeyUrlSuggestion, kDefaultUrlSuggestion));
  nsoption_set_bool(target_blank,
                    ReadBoolOrDefault(kKeyTargetBlank, kDefaultTargetBlank));
  nsoption_set_bool(button_2_tab,
                    ReadBoolOrDefault(kKeyButton2Tab, kDefaultButton2Tab));
  nsoption_set_bool(foreground_new,
                    ReadBoolOrDefault(kKeyForegroundNew, kDefaultForegroundNew));

  nsoption_set_bool(
      prefer_dark_mode,
      ReadBoolOrDefault(kKeyPreferDarkMode, kDefaultPreferDarkMode));

  int scale_pct = std::clamp(
      ReadIntOrDefault(kKeyScale, kDefaultScalePercent), kMinScalePercent,
      kMaxScalePercent);
  nsoption_set_int(scale, scale_pct);
  if (is_initial_load) {
    last_applied_scale = scale_pct;
  } else if (scale_pct != last_applied_scale) {
    last_applied_scale = scale_pct;
    for (auto* gw : GetOpenTabs()) {
      if (gw && gw->GetBrowserWindow())
        browser_window_set_scale(gw->GetBrowserWindow(),
                                 static_cast<float>(scale_pct) / 100.0f, true);
    }
  }

  nsoption_set_bool(
      author_level_css,
      ReadBoolOrDefault(kKeyAuthorLevelCss, kDefaultAuthorLevelCss));

  int font_default_opt =
      ReadIntOrDefault(kKeyFontDefault, kDefaultFontFamilyOption);
  int plot_font_family = PLOT_FONT_FAMILY_SANS_SERIF;
  if (font_default_opt >= 1 && font_default_opt <= PLOT_FONT_FAMILY_COUNT)
    plot_font_family = font_default_opt - 1;
  else if (font_default_opt >= 0 && font_default_opt < PLOT_FONT_FAMILY_COUNT)
    plot_font_family = font_default_opt;
  nsoption_set_int(font_default, plot_font_family);

  int font_size_pt =
      std::max(1, ReadIntOrDefault(kKeyFontSize, kDefaultFontSizePt));
  nsoption_set_int(font_size, font_size_pt * kDecipointsPerPoint);

  int font_min_size_pt =
      std::max(1, ReadIntOrDefault(kKeyFontMinSize, kDefaultFontMinSizePt));
  nsoption_set_int(font_min_size, font_min_size_pt * kDecipointsPerPoint);

  int treeview_font_size_pt = std::max(
      1, ReadIntOrDefault(kKeyTreeviewFontSize, kDefaultTreeviewFontSizePt));
  nsoption_set_int(treeview_font_size,
                   treeview_font_size_pt * kDecipointsPerPoint);

  nsoption_set_bool(
      enable_javascript,
      ReadBoolOrDefault(kKeyEnableJavascript, kDefaultEnableJavascript));
  nsoption_set_int(
      script_timeout,
      std::max(1, ReadIntOrDefault(kKeyScriptTimeout,
                                   kDefaultScriptTimeoutSeconds)));

  bool fallback_images =
      ReadBool(kKeyLoadImages).value_or(kDefaultForegroundImages);
  bool fg_images = ReadBool(kKeyForegroundImages).value_or(fallback_images);
  bool bg_images = ReadBool(kKeyBackgroundImages).value_or(
      ReadBool(kKeyLoadImages).value_or(kDefaultBackgroundImages));
  nsoption_set_bool(foreground_images, fg_images);
  nsoption_set_bool(background_images, bg_images);
  nsoption_set_bool(animate_images,
                    ReadBoolOrDefault(kKeyAnimateImages, kDefaultAnimateImages));

  nsoption_set_bool(
      incremental_reflow,
      ReadBoolOrDefault(kKeyIncrementalReflow, kDefaultIncrementalReflow));
  int min_reflow_ms =
      ReadIntOrDefault(kKeyMinReflowPeriod, kDefaultMinReflowPeriodMs);
  int min_reflow_cs = std::max(1, min_reflow_ms / kMillisecondsPerCentisecond);
  nsoption_set_int(min_reflow_period, min_reflow_cs);

  nsoption_set_bool(
      block_advertisements,
      ReadBoolOrDefault(kKeyBlockAdvertisements, kDefaultBlockAdvertisements));
  nsoption_set_bool(do_not_track,
                    ReadBoolOrDefault(kKeyDoNotTrack, kDefaultDoNotTrack));
  nsoption_set_bool(send_referer,
                    ReadBoolOrDefault(kKeySendReferer, kDefaultSendReferer));
  nsoption_set_bool(
      display_decoded_idn,
      ReadBoolOrDefault(kKeyDisplayDecodedIdn, kDefaultDisplayDecodedIdn));

  nsoption_set_int(
      expire_url,
      std::max(1, ReadIntOrDefault(kKeyExpireUrl, kDefaultExpireUrlDays)));
  persist_storage_enabled =
      ReadBoolOrDefault(kKeyPersistStorage, kDefaultPersistStorage);

  std::string accept_lang =
      ReadStringOrDefault(kKeyAcceptLanguage, kDefaultAcceptLanguage);
  nsoption_set_charp(accept_language, strdup(accept_lang.c_str()));

  std::string accept_charset =
      ReadStringOrDefault(kKeyAcceptCharset, kDefaultAcceptCharset);
  nsoption_set_charp(accept_charset, strdup(accept_charset.c_str()));

  nsoption_set_int(
      max_fetchers,
      std::max(1, ReadIntOrDefault(kKeyMaxFetchers, kDefaultMaxFetchers)));
  nsoption_set_int(
      max_fetchers_per_host,
      std::max(1, ReadIntOrDefault(kKeyMaxFetchersPerHost,
                                   kDefaultMaxFetchersPerHost)));
  nsoption_set_int(
      max_retried_fetches,
      std::max(0, ReadIntOrDefault(kKeyMaxRetriedFetches,
                                   kDefaultMaxRetriedFetches)));
  nsoption_set_int(
      fetch_redirect_limit,
      std::max(0, ReadIntOrDefault(kKeyFetchRedirectLimit,
                                   kDefaultFetchRedirectLimit)));

  int mem_cache_mb =
      std::max(0, ReadIntOrDefault(kKeyMemoryCacheSize,
                                   kDefaultMemoryCacheSizeMb));
  nsoption_set_int(memory_cache_size, mem_cache_mb * kBytesPerMegabyte);

  int disc_cache_mb =
      std::max(0, ReadIntOrDefault(kKeyDiscCacheSize, kDefaultDiscCacheSizeMb));
  nsoption_set_int(disc_cache_size, disc_cache_mb * kBytesPerMegabyte);

  nsoption_set_int(
      disc_cache_age,
      std::max(0, ReadIntOrDefault(kKeyDiscCacheAge, kDefaultDiscCacheAgeDays)));

  nsoption_set_bool(http_proxy,
                    ReadBoolOrDefault(kKeyHttpProxy, kDefaultHttpProxy));
  std::string proxy_host = ReadStringOrDefault(kKeyHttpProxyHost, "");
  nsoption_set_charp(http_proxy_host, strdup(proxy_host.c_str()));
  nsoption_set_int(http_proxy_port,
                   ReadIntOrDefault(kKeyHttpProxyPort, kDefaultHttpProxyPort));
  nsoption_set_int(http_proxy_auth,
                   ReadIntOrDefault(kKeyHttpProxyAuth, kDefaultHttpProxyAuth));
  std::string proxy_user = ReadStringOrDefault(kKeyHttpProxyAuthUser, "");
  nsoption_set_charp(http_proxy_auth_user, strdup(proxy_user.c_str()));
  std::string proxy_pass = ReadStringOrDefault(kKeyHttpProxyAuthPass, "");
  nsoption_set_charp(http_proxy_auth_pass, strdup(proxy_pass.c_str()));
  std::string proxy_noproxy =
      ReadStringOrDefault(kKeyHttpProxyNoProxy, kDefaultHttpProxyNoProxy);
  nsoption_set_charp(http_proxy_noproxy, strdup(proxy_noproxy.c_str()));

  for (const auto& config : kSystemColors)
    LoadColorSetting(config);
}

void ScheduleSettingsRefresh() {
  if (settings_refresh_deferred)
    return;
  settings_refresh_deferred = true;
  ::perception::Defer([]() {
    NETSURF_LOCK;
    settings_refresh_deferred = false;
    ApplySettings(false);
    InvalidateAllTabs();
  });
}

}  // namespace

void SetSystemColorDefaults(struct nsoption_s* defaults) {
  for (const auto& config : kSystemColors)
    defaults[config.nsc].value.c = config.default_colour;
}

bool IsStoragePersistenceEnabled() { return persist_storage_enabled; }

const std::string& GetSearchUrlTemplate() { return search_url_template; }

const std::string& GetSearchUrl() { return search_url_prefix; }

std::string BuildNavigationUrlFromInput(std::string_view input) {
  size_t start = input.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos)
    return "";
  size_t end = input.find_last_not_of(" \t\r\n");
  std::string_view trimmed = input.substr(start, end - start + 1);

  if (trimmed.find("://") != std::string_view::npos)
    return std::string(trimmed);

  if (trimmed.rfind("about:", 0) == 0 || trimmed.rfind("data:", 0) == 0 ||
      trimmed.rfind("resource:", 0) == 0 ||
      trimmed.rfind("javascript:", 0) == 0)
    return std::string(trimmed);

  if (trimmed.front() == '/')
    return "file://" + std::string(trimmed);

  if (trimmed.find_first_of(" \t\r\n") == std::string_view::npos) {
    std::string_view host_part = trimmed.substr(0, trimmed.find_first_of("/?"));
    bool is_localhost =
        host_part == "localhost" || host_part.rfind("localhost:", 0) == 0;
    bool has_domain_dot = host_part.find('.') != std::string_view::npos &&
                          host_part.front() != '.' && host_part.back() != '.';
    if (is_localhost || has_domain_dot)
      return "https://" + std::string(trimmed);
  }

  std::string search_tmpl = GetSearchUrlTemplate();
  if (search_tmpl.find("://") == std::string::npos)
    search_tmpl = "https://" + search_tmpl;

  std::string query_str(trimmed);
  char* escaped = nullptr;
  std::string escaped_query;
  if (url_escape(query_str.c_str(), true, nullptr, &escaped) == NSERROR_OK &&
      escaped != nullptr) {
    escaped_query = escaped;
    free(escaped);
  } else {
    escaped_query = query_str;
  }

  size_t placeholder_pos = search_tmpl.find(kSearchQueryPlaceholder);
  if (placeholder_pos != std::string::npos) {
    search_tmpl.replace(placeholder_pos, kSearchQueryPlaceholder.size(),
                        escaped_query);
    return search_tmpl;
  }
  return search_tmpl + escaped_query;
}

void RegisterSettingsListeners() {
  static bool registered = false;
  if (registered)
    return;
  registered = true;

  for (std::string_view key : kWatchedSettingKeys) {
    auto token_or =
        ::perception::RegisterRegistryListener(key, ScheduleSettingsRefresh);
    if (token_or.Ok())
      listener_tokens.push_back(*token_or);
  }

  for (const auto& config : kSystemColors) {
    auto token_or = ::perception::RegisterRegistryListener(
        config.key, ScheduleSettingsRefresh);
    if (token_or.Ok())
      listener_tokens.push_back(*token_or);
  }
}

void LoadSettingsFromRegistry() {
  ApplySettings(true);
  RegisterSettingsListeners();
}

}  // namespace perception
}  // namespace netsurf
