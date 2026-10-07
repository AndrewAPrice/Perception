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

#include "window.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bitmap.h"
#include "devtools.h"
#include "download.h"
#include "managers.h"
#include "misc.h"
#include "plotters.h"
#include "settings.h"
#include "tabs.h"

extern "C" {
#include <libwapcaplet/libwapcaplet.h>

#include "content/fetch.h"
#include "content/fetchers.h"
#include "content/urldb.h"
#include "desktop/browser_history.h"
#include "desktop/save_complete.h"
#include "desktop/save_text.h"
#include "desktop/search.h"
#include "netsurf/bitmap.h"
#include "netsurf/browser_window.h"
#include "netsurf/clipboard.h"
#include "netsurf/content.h"
#include "netsurf/content_type.h"
#include "netsurf/cookie_db.h"
#include "netsurf/fetch.h"
#include "netsurf/keypress.h"
#include "netsurf/layout.h"
#include "netsurf/misc.h"
#include "netsurf/mouse.h"
#include "netsurf/netsurf.h"
#include "netsurf/plot_style.h"
#include "netsurf/plotters.h"
#include "netsurf/search.h"
#include "netsurf/url_db.h"
#include "netsurf/window.h"
#include "utils/errors.h"
#include "utils/file.h"
#include "utils/filepath.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"
#include "utils/url.h"
content_status content_get_status(struct hlcache_handle* h);
}

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkRect.h"
#include "include/core/SkString.h"
#include "perception/clipboard.h"
#include "perception/fibers.h"
#include "perception/processes.h"
#include "perception/random.h"
#include "perception/scheduler.h"
#include "perception/time.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/checkbox.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/file_dialog.h"
#include "perception/ui/components/focusable.h"
#include "perception/ui/components/image_button.h"
#include "perception/ui/components/image_view.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/pop_up.h"
#include "perception/ui/components/resizable_container.h"
#include "perception/ui/components/scroll_bar.h"
#include "perception/ui/components/tab_bar.h"
#include "perception/ui/components/tooltip.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/font.h"
#include "perception/ui/image.h"
#include "perception/ui/keyboard.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/resize_method.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/theme.h"

using ::perception::HandOverControl;
using ::perception::TerminateProcess;
using ::perception::ui::DrawContext;
using ::perception::ui::Image;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::ResizeMethod;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Checkbox;
using ::perception::ui::components::Container;
using ::perception::ui::components::Focusable;
using ::perception::ui::components::ImageButton;
using ::perception::ui::components::ImageView;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::PopUp;
using ::perception::ui::components::PopUpMenu;
using ::perception::ui::components::ResizableContainer;
using ::perception::ui::components::ResizableContainerItem;
using ::perception::ui::components::ScrollBar;
using ::perception::ui::components::ShowOpenFileDialog;
using ::perception::ui::components::ShowSaveFileDialog;
using ::perception::ui::components::TabBar;
using ::perception::ui::components::Tooltip;
using ::perception::ui::components::UiWindow;

namespace {

// Path to the back icon asset.
constexpr std::string_view kBackIconPath = "/Applications/NetSurf/back.svg";

// Path to the forward icon asset.
constexpr std::string_view kForwardIconPath =
    "/Applications/NetSurf/forward.svg";

// Path to the refresh icon asset.
constexpr std::string_view kRefreshIconPath =
    "/Applications/NetSurf/refresh.svg";

// Path to the close/stop icon asset.
constexpr std::string_view kCloseIconPath = "/Applications/NetSurf/close.svg";

// Path to the plus (new tab) icon asset.
constexpr std::string_view kPlusIconPath = "/Applications/NetSurf/plus.svg";

// Path to the outline star icon asset.
constexpr std::string_view kStarIconPath = "/Applications/NetSurf/star.svg";

// Path to the filled star icon asset.
constexpr std::string_view kStarFilledIconPath =
    "/Applications/NetSurf/star-filled.svg";

// Path to the vertical overflow menu icon asset.
constexpr std::string_view kMoreVerticalIconPath =
    "/Applications/NetSurf/more-vertical.svg";

// Path to the search icon asset.
constexpr std::string_view kSearchIconPath = "/Applications/NetSurf/search.svg";

// Path to the chevron-up (find previous) icon asset.
constexpr std::string_view kChevronUpIconPath =
    "/Applications/NetSurf/chevron-up.svg";

// Path to the chevron-down (find next) icon asset.
constexpr std::string_view kChevronDownIconPath =
    "/Applications/NetSurf/chevron-down.svg";

// Path to the case-sensitive toggle icon asset.
constexpr std::string_view kCaseSensitiveIconPath =
    "/Applications/NetSurf/case-sensitive.svg";

// Path to the highlight-all toggle icon asset.
constexpr std::string_view kHighlightIconPath =
    "/Applications/NetSurf/highlight.svg";

// Path to the bookmark icon asset.
constexpr std::string_view kBookmarkIconPath =
    "/Applications/NetSurf/bookmark.svg";

// Path to the bookmark-plus icon asset.
constexpr std::string_view kBookmarkPlusIconPath =
    "/Applications/NetSurf/bookmark-plus.svg";

// Path to the history icon asset.
constexpr std::string_view kHistoryIconPath =
    "/Applications/NetSurf/history.svg";

// Path to the download icon asset.
constexpr std::string_view kDownloadIconPath =
    "/Applications/NetSurf/download.svg";

// Path to the cookie icon asset.
constexpr std::string_view kCookieIconPath = "/Applications/NetSurf/cookie.svg";

// Path to the save icon asset.
constexpr std::string_view kSaveIconPath = "/Applications/NetSurf/save.svg";

// Path to the source code icon asset.
constexpr std::string_view kCodeIconPath = "/Applications/NetSurf/code.svg";

// Path to the terminal / developer tools icon asset.
constexpr std::string_view kTerminalIconPath =
    "/Applications/NetSurf/terminal.svg";

// Path to the copy icon asset.
constexpr std::string_view kCopyIconPath = "/Applications/NetSurf/copy.svg";

// Path to the external-link icon asset.
constexpr std::string_view kExternalLinkIconPath =
    "/Applications/NetSurf/external-link.svg";

// Path to the zoom-in icon asset.
constexpr std::string_view kZoomInIconPath =
    "/Applications/NetSurf/zoom-in.svg";

// Path to the zoom-out icon asset.
constexpr std::string_view kZoomOutIconPath =
    "/Applications/NetSurf/zoom-out.svg";

// Path to the error alert-circle icon asset.
constexpr std::string_view kAlertCircleIconPath =
    "/Applications/NetSurf/alert-circle.svg";

// Path to the warning alert-triangle icon asset.
constexpr std::string_view kAlertTriangleIconPath =
    "/Applications/NetSurf/alert-triangle.svg";

// Path to the verified HTTPS security icon asset.
constexpr std::string_view kPageInfoSecureIconPath =
    "/Applications/NetSurf/res/icons/page-info-secure.svg";

// Path to the insecure HTTP security icon asset.
constexpr std::string_view kPageInfoInsecureIconPath =
    "/Applications/NetSurf/res/icons/page-info-insecure.svg";

// Path to the warning security icon asset.
constexpr std::string_view kPageInfoWarningIconPath =
    "/Applications/NetSurf/res/icons/page-info-warning.svg";

// Path to the local resource security icon asset.
constexpr std::string_view kPageInfoLocalIconPath =
    "/Applications/NetSurf/res/icons/page-info-local.svg";

// Path to the internal page security icon asset.
constexpr std::string_view kPageInfoInternalIconPath =
    "/Applications/NetSurf/res/icons/page-info-internal.svg";

// Threshold in pixels before a mouse press transitions to a drag operation.
constexpr int kDragThresholdPixels = 5;

// Maximum interval in milliseconds between clicks for multi-click detection.
constexpr int64_t kDoubleClickTimeMs = 400;

// Default caret height in pixels if none is specified.
constexpr int kDefaultCaretHeight = 14;

// Scroll distance in pixels for arrow key viewport scrolling.
constexpr float kLineScrollStep = 40.0f;

// Fraction of viewport height scrolled for PageUp, PageDown, and Space.
constexpr float kPageScrollFraction = 0.85f;

// Minimum viewport width or height in pixels for NetSurf layout formatting.
constexpr int kMinViewportDimension = 32;

// Fallback viewport width in pixels before layout calculation completes.
constexpr int kFallbackViewportWidth = 800;

// Fallback viewport height in pixels before layout calculation completes.
constexpr int kFallbackViewportHeight = 600;

// Height of the unified omnibox container in pixels.
constexpr float kOmniboxHeight = 28.0f;

// Size (width and height) of action buttons inside the omnibox in pixels.
constexpr float kOmniboxButtonSize = 24.0f;

// Size (width and height) of icons inside omnibox action buttons in pixels.
constexpr float kOmniboxIconSize = 16.0f;

// Horizontal padding inside the unified omnibox container in pixels.
constexpr float kOmniboxHorizontalPadding = 4.0f;

// Maximum number of URL autocomplete suggestions shown below the omnibox.
constexpr size_t kMaxUrlSuggestions = 8;

// Height in pixels of the loading progress indicator line below the toolbar.
constexpr float kLoadingIndicatorHeight = 2.0f;

// Accent color of the loading progress indicator line.
constexpr uint32_t kLoadingIndicatorColor = 0xFF3B82F6;

// Width in pixels of the search query input box in the Find-in-Page bar.
constexpr float kFindInputWidth = 220.0f;

// Size in pixels of the leading search icon in the Find-in-Page bar.
constexpr float kFindBarIconSize = 16.0f;

// Height in pixels of the inline zoom control row in the overflow menu.
constexpr float kZoomRowHeight = 28.0f;

// Horizontal padding in pixels of the inline zoom control row.
constexpr float kZoomRowHorizontalPadding = 12.0f;

// Zoom scale increment/decrement step (10%).
constexpr float kZoomStep = 0.1f;

// Minimum allowed browser page zoom scale (25%).
constexpr float kMinZoomScale = 0.25f;

// Maximum allowed browser page zoom scale (400%).
constexpr float kMaxZoomScale = 4.0f;

// Height in pixels of the bottom status bar.
constexpr float kStatusBarHeight = 22.0f;

// Height in pixels of clickable badges in the bottom status bar.
constexpr float kStatusBarBadgeHeight = 20.0f;

// Size in pixels of icons inside bottom status bar badges.
constexpr float kStatusBarBadgeIconSize = 14.0f;

// Horizontal padding in pixels of the bottom status bar.
constexpr float kStatusBarHorizontalPadding = 8.0f;

// Horizontal padding in pixels inside bottom status bar badge buttons.
constexpr float kStatusBarBadgePadding = 6.0f;

// Tint color for verified secure page info icons.
constexpr uint32_t kSecureIconTint = 0xFF16A34A;

// Tint color for insecure page info icons and console error badges.
constexpr uint32_t kInsecureIconTint = 0xFFDC2626;

// Tint color for warning page info icons and console warning badges.
constexpr uint32_t kWarningIconTint = 0xFFD97706;

// Tint color for neutral/default page info icons.
constexpr uint32_t kDefaultSecurityIconTint = 0xFF6B7280;

// Tint color for bookmarked filled star icon.
constexpr uint32_t kBookmarkedStarTint = 0xFFF59E0B;

// White tint color for icons placed on the dark window tab bar.
constexpr uint32_t kWhiteIconTint = 0xFFFFFFFF;

}  // namespace

namespace netsurf {
namespace perception {

namespace {

// Global toolbar and status bar widget pointers.
Button* g_back_button = nullptr;
Button* g_forward_button = nullptr;
ImageButton* g_reload_stop_image_button = nullptr;
std::shared_ptr<Node> g_reload_stop_button_node = nullptr;
ImageButton* g_security_image_button = nullptr;
std::shared_ptr<Node> g_security_button_node = nullptr;
ImageButton* g_bookmark_star_image_button = nullptr;
std::shared_ptr<Node> g_bookmark_star_button_node = nullptr;
std::shared_ptr<Node> g_omnibox_node = nullptr;
std::shared_ptr<Node> g_overflow_button_node = nullptr;
std::shared_ptr<Node> g_loading_indicator_node = nullptr;
std::shared_ptr<PopUp> g_url_suggestion_popup = nullptr;
bool g_suppress_url_suggestions = false;

// Find-in-Page bar state and widget pointers.
std::shared_ptr<Node> g_find_bar_node = nullptr;
InputBox* g_find_input = nullptr;
Label* g_find_status_label = nullptr;
Button* g_find_prev_button = nullptr;
Button* g_find_next_button = nullptr;
Button* g_find_case_button = nullptr;
Button* g_find_highlight_button = nullptr;
bool g_find_bar_open = false;
bool g_find_case_sensitive = false;
bool g_find_show_all = false;

// Docked Developer Tools split container state.
std::shared_ptr<Node> g_viewport_devtools_split = nullptr;

// Bottom status bar badge widget pointers.
std::shared_ptr<Node> g_download_badge_node = nullptr;
Label* g_download_badge_label = nullptr;
std::shared_ptr<ImageView> g_console_badge_image_view = nullptr;
Label* g_console_badge_label = nullptr;

// Temporary collector used during urldb_iterate_partial callbacks.
std::vector<std::pair<std::string, std::string>>* g_suggestion_collector =
    nullptr;

bool UrlSuggestionCollectCallback(struct nsurl* url,
                                  const struct url_data* data) {
  if (!g_suggestion_collector || !url)
    return false;
  if (g_suggestion_collector->size() >= kMaxUrlSuggestions)
    return false;
  if (data && data->visits == 0)
    return true;
  const char* raw_url = nsurl_access(url);
  if (!raw_url || raw_url[0] == '\0')
    return true;
  std::string url_str(raw_url);
  for (const auto& existing : *g_suggestion_collector) {
    if (existing.first == url_str)
      return true;
  }
  std::string title_str = (data && data->title) ? data->title : "";
  g_suggestion_collector->emplace_back(std::move(url_str),
                                       std::move(title_str));
  return g_suggestion_collector->size() < kMaxUrlSuggestions;
}

void CloseUrlSuggestionPopup() {
  if (g_url_suggestion_popup) {
    g_url_suggestion_popup->Close();
    g_url_suggestion_popup.reset();
  }
}

void NavigateActiveTabToInput(std::string_view text) {
  CloseUrlSuggestionPopup();
  if (!GetActiveTab() || !GetActiveTab()->GetBrowserWindow())
    return;
  std::string url_str = BuildNavigationUrl(text);
  if (url_str.empty())
    return;
  struct nsurl* url = nullptr;
  if (nsurl_create(url_str.c_str(), &url) == NSERROR_OK && url != nullptr) {
    browser_window_navigate(GetActiveTab()->GetBrowserWindow(), url, nullptr,
                            BW_NAVIGATE_HISTORY, nullptr, nullptr, nullptr);
    nsurl_unref(url);
  }
}

void UpdateUrlSuggestions(std::string_view input_text) {
  if (g_suppress_url_suggestions)
    return;

  CloseUrlSuggestionPopup();

  auto* url_input = GetGlobalUrlInput();
  if (!url_input || !url_input->HasFocus() || !g_omnibox_node ||
      !nsoption_bool(url_suggestion)) {
    return;
  }

  size_t first = input_text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos)
    return;
  size_t last = input_text.find_last_not_of(" \t\r\n");
  std::string trimmed(input_text.substr(first, last - first + 1));
  if (trimmed.empty())
    return;

  std::vector<std::pair<std::string, std::string>> matches;
  g_suggestion_collector = &matches;
  urldb_iterate_partial(trimmed.c_str(), UrlSuggestionCollectCallback);
  if (matches.size() < kMaxUrlSuggestions &&
      trimmed.find("://") == std::string::npos) {
    std::string https_prefix = "https://" + trimmed;
    urldb_iterate_partial(https_prefix.c_str(), UrlSuggestionCollectCallback);
  }
  if (matches.size() < kMaxUrlSuggestions &&
      trimmed.find("://") == std::string::npos) {
    std::string http_prefix = "http://" + trimmed;
    urldb_iterate_partial(http_prefix.c_str(), UrlSuggestionCollectCallback);
  }
  g_suggestion_collector = nullptr;

  if (matches.empty() ||
      (matches.size() == 1 && matches[0].first == trimmed)) {
    return;
  }

  float omnibox_w = g_omnibox_node->GetSize().width;
  auto menu = PopUpMenu::Container([omnibox_w](Layout& layout) {
    if (omnibox_w > 0.0f)
      layout.SetWidth(omnibox_w);
  });

  auto globe_icon = Image::LoadImage(kSearchIconPath);
  for (const auto& [matched_url, matched_title] : matches) {
    std::string url_copy = matched_url;
    menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
        matched_url, globe_icon, matched_title, [url_copy]() {
          NETSURF_LOCK;
          if (auto* input = GetGlobalUrlInput()) {
            g_suppress_url_suggestions = true;
            input->SetText(url_copy);
            g_suppress_url_suggestions = false;
          }
          NavigateActiveTabToInput(url_copy);
        }));
  }

  Point pos = g_omnibox_node->GetAbsolutePosition();
  auto size = g_omnibox_node->GetSize();
  g_url_suggestion_popup =
      PopUp::Show(g_omnibox_node, Point{pos.x, pos.y + size.height}, menu,
                  []() { g_url_suggestion_popup.reset(); });
}

void OpenNewTab(const char* target_url_override = nullptr,
                bool force_foreground = true) {
  const char* target_url = target_url_override;
  if (!target_url || target_url[0] == '\0')
    target_url = nsoption_charp(homepage_url);
  if (!target_url || target_url[0] == '\0')
    target_url = "about:welcome";

  struct nsurl* url = nullptr;
  if (nsurl_create(target_url, &url) != NSERROR_OK || url == nullptr)
    return;

  struct browser_window* existing_bw =
      GetActiveTab() ? GetActiveTab()->GetBrowserWindow() : nullptr;
  int create_flags = BW_CREATE_TAB | BW_CREATE_HISTORY;
  if (force_foreground || nsoption_bool(foreground_new))
    create_flags |= BW_CREATE_FOREGROUND;
  if (target_url_override == nullptr)
    create_flags |= BW_CREATE_FOCUS_LOCATION;

  struct browser_window* new_bw = nullptr;
  browser_window_create(
      static_cast<browser_window_create_flags>(create_flags), url, nullptr,
      existing_bw, &new_bw);
  nsurl_unref(url);
}

void AdjustZoom(float delta_scale) {
  if (!GetActiveTab() || !GetActiveTab()->GetBrowserWindow())
    return;
  struct browser_window* bw = GetActiveTab()->GetBrowserWindow();
  float current = browser_window_get_scale(bw);
  float next = std::clamp(std::round((current + delta_scale) * 10.0f) / 10.0f,
                          kMinZoomScale, kMaxZoomScale);
  browser_window_set_scale(bw, next, true);
  UpdateScrollBars(GetActiveTab());
  if (GetActiveTab()->GetContentNode())
    GetActiveTab()->GetContentNode()->Invalidate();
}

void ResetZoom() {
  if (!GetActiveTab() || !GetActiveTab()->GetBrowserWindow())
    return;
  struct browser_window* bw = GetActiveTab()->GetBrowserWindow();
  browser_window_set_scale(bw, 1.0f, true);
  UpdateScrollBars(GetActiveTab());
  if (GetActiveTab()->GetContentNode())
    GetActiveTab()->GetContentNode()->Invalidate();
}

void ToggleActivePageBookmark() {
  if (!GetActiveTab() || !GetActiveTab()->GetBrowserWindow())
    return;
  struct nsurl* url = nullptr;
  if (browser_window_get_url(GetActiveTab()->GetBrowserWindow(), true, &url) ==
          NSERROR_OK &&
      url != nullptr) {
    ToggleBookmarkForUrl(url);
    nsurl_unref(url);
    UpdateBrowserToolbar();
  }
}

void EditActivePageBookmarkDialog() {
  if (!GetActiveTab() || !GetActiveTab()->GetBrowserWindow())
    return;
  struct nsurl* url = nullptr;
  if (browser_window_get_url(GetActiveTab()->GetBrowserWindow(), true, &url) ==
          NSERROR_OK &&
      url != nullptr) {
    AddBookmarkDialog(url, GetActiveTab()->GetTitle(), GetGlobalUiWindow());
    nsurl_unref(url);
    UpdateBrowserToolbar();
  }
}

bool EndsWithCaseInsensitive(std::string_view str, std::string_view suffix) {
  if (suffix.size() > str.size())
    return false;
  size_t offset = str.size() - suffix.size();
  for (size_t i = 0; i < suffix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(str[offset + i])) !=
        std::tolower(static_cast<unsigned char>(suffix[i]))) {
      return false;
    }
  }
  return true;
}

void SaveActivePage() {
  if (!GetActiveTab() || !GetActiveTab()->GetBrowserWindow())
    return;
  struct browser_window* bw = GetActiveTab()->GetBrowserWindow();
  struct hlcache_handle* content = browser_window_get_content(bw);
  if (!content)
    return;

  std::string default_name = "page.html";
  struct nsurl* url = nullptr;
  if (browser_window_get_url(bw, false, &url) == NSERROR_OK && url != nullptr) {
    char* nice = nullptr;
    if (nsurl_nice(url, &nice, false) == NSERROR_OK && nice != nullptr) {
      default_name = nice;
      free(nice);
      if (!EndsWithCaseInsensitive(default_name, ".html") &&
          !EndsWithCaseInsensitive(default_name, ".htm")) {
        default_name += ".html";
      }
    }
    nsurl_unref(url);
  }

  auto is_alive = GetActiveTab()->GetIsAlive();
  ShowSaveFileDialog(
      [bw, is_alive](bool succeeded, std::string_view path) {
        if (!succeeded || path.empty() || !*is_alive)
          return;
        std::string path_str(path);
        NETSURF_LOCK;
        struct hlcache_handle* h = browser_window_get_content(bw);
        if (!h)
          return;
        if (EndsWithCaseInsensitive(path_str, ".txt")) {
          save_as_text(h, path_str.data());
          return;
        }
        size_t source_size = 0;
        const uint8_t* source_data = content_get_source_data(h, &source_size);
        if (source_data != nullptr && source_size > 0) {
          FILE* f = fopen(path_str.c_str(), "wb");
          if (f != nullptr) {
            fwrite(source_data, 1, source_size, f);
            fclose(f);
          }
        }
      },
      {"html", "htm", "txt"}, default_name, "", "Save Page As",
      GetGlobalUiWindow());
}

void RunFindInPage(bool forward) {
  if (!GetActiveTab() || !GetActiveTab()->GetBrowserWindow() || !g_find_input)
    return;
  struct browser_window* bw = GetActiveTab()->GetBrowserWindow();
  std::string query = g_find_input->GetText();
  if (query.empty()) {
    browser_window_search_clear(bw);
    if (g_find_status_label)
      g_find_status_label->SetText("");
    return;
  }

  int flags = forward ? SEARCH_FLAG_FORWARDS : SEARCH_FLAG_BACKWARDS;
  if (g_find_case_sensitive)
    flags |= SEARCH_FLAG_CASE_SENSITIVE;
  if (g_find_show_all)
    flags |= SEARCH_FLAG_SHOWALL;

  browser_window_search(bw, nullptr, static_cast<search_flags_t>(flags),
                        query.c_str());
}

void ShowFindBar() {
  if (!g_find_bar_node)
    return;
  g_find_bar_open = true;
  g_find_bar_node->GetLayout().SetDisplay(YGDisplayFlex);
  if (g_find_input) {
    g_find_input->Focus();
    g_find_input->SelectAll();
    if (!g_find_input->GetText().empty())
      RunFindInPage(true);
  }
  if (GetGlobalUiWindow())
    GetGlobalUiWindow()->Invalidate();
}

void CloseFindBar() {
  if (!g_find_bar_node)
    return;
  g_find_bar_open = false;
  g_find_bar_node->GetLayout().SetDisplay(YGDisplayNone);
  if (GetActiveTab() && GetActiveTab()->GetBrowserWindow())
    browser_window_search_clear(GetActiveTab()->GetBrowserWindow());
  if (g_find_status_label)
    g_find_status_label->SetText("");
  if (GetGlobalUiWindow())
    GetGlobalUiWindow()->Invalidate();
}

void ShowOverflowMenu(std::shared_ptr<Node> anchor_node) {
  if (!anchor_node)
    return;

  auto plus_icon = Image::LoadImage(kPlusIconPath);
  auto zoom_out_icon = Image::LoadImage(kZoomOutIconPath);
  auto zoom_in_icon = Image::LoadImage(kZoomInIconPath);
  auto search_icon = Image::LoadImage(kSearchIconPath);
  auto bookmark_icon = Image::LoadImage(kBookmarkIconPath);
  auto history_icon = Image::LoadImage(kHistoryIconPath);
  auto download_icon = Image::LoadImage(kDownloadIconPath);
  auto cookie_icon = Image::LoadImage(kCookieIconPath);
  auto save_icon = Image::LoadImage(kSaveIconPath);
  auto code_icon = Image::LoadImage(kCodeIconPath);
  auto terminal_icon = Image::LoadImage(kTerminalIconPath);

  int initial_zoom_pct = 100;
  if (GetActiveTab() && GetActiveTab()->GetBrowserWindow()) {
    initial_zoom_pct = static_cast<int>(std::round(
        browser_window_get_scale(GetActiveTab()->GetBrowserWindow()) * 100.0f));
  }

  Label* zoom_pct_label_ptr = nullptr;
  auto zoom_pct_label_node = Label::BasicLabel(
      std::to_string(initial_zoom_pct) + "%", &zoom_pct_label_ptr);
  auto update_zoom_label = [zoom_pct_label_ptr]() {
    if (!zoom_pct_label_ptr || !GetActiveTab() ||
        !GetActiveTab()->GetBrowserWindow()) {
      return;
    }
    int pct = static_cast<int>(std::round(
        browser_window_get_scale(GetActiveTab()->GetBrowserWindow()) * 100.0f));
    zoom_pct_label_ptr->SetText(std::to_string(pct) + "%");
  };

  auto zoom_row = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetHeight(kZoomRowHeight);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetPadding(YGEdgeHorizontal, kZoomRowHorizontalPadding);
      },
      Label::BasicLabel(
          "Zoom", [](Layout& l) { l.SetFlexGrow(1.0f); },
          [](Label& lbl) {
            lbl.SetColor(::perception::ui::kPopUpItemTextColor);
            lbl.SetTextAlignment(TextAlignment::MiddleLeft);
          }),
      ImageButton::BasicImageButton(
          [update_zoom_label]() {
            NETSURF_LOCK;
            AdjustZoom(-kZoomStep);
            update_zoom_label();
          },
          zoom_out_icon, Tooltip::ShowTooltip("Zoom Out (Ctrl+-)")),
      Button::BasicButton(
          [update_zoom_label]() {
            NETSURF_LOCK;
            ResetZoom();
            update_zoom_label();
          },
          [](Button& btn) { btn.SetButtonStyle(Button::ButtonStyle::GHOST); },
          [](Layout& l) {
            l.SetMinWidth(48.0f);
            l.SetHeight(kOmniboxButtonSize);
            l.SetMinHeight(kOmniboxButtonSize);
          },
          Tooltip::ShowTooltip("Reset Zoom (Ctrl+0)"), zoom_pct_label_node),
      ImageButton::BasicImageButton(
          [update_zoom_label]() {
            NETSURF_LOCK;
            AdjustZoom(kZoomStep);
            update_zoom_label();
          },
          zoom_in_icon, Tooltip::ShowTooltip("Zoom In (Ctrl++)")));

  auto menu = PopUpMenu::Container(
      PopUpMenu::ItemWithIconAndShortcut("New Tab", plus_icon, "Ctrl+T",
                                         []() {
                                           NETSURF_LOCK;
                                           OpenNewTab();
                                         }),
      PopUpMenu::Divider(), zoom_row, PopUpMenu::Divider(),
      PopUpMenu::ItemWithIconAndShortcut("Find in Page...", search_icon,
                                         "Ctrl+F",
                                         []() {
                                           NETSURF_LOCK;
                                           ShowFindBar();
                                         }),
      PopUpMenu::ItemWithIconAndShortcut("Bookmarks...", bookmark_icon,
                                         "Ctrl+B",
                                         []() {
                                           NETSURF_LOCK;
                                           ShowBookmarksWindow();
                                         }),
      PopUpMenu::ItemWithIconAndShortcut("History...", history_icon, "Ctrl+H",
                                         []() {
                                           NETSURF_LOCK;
                                           ShowGlobalHistoryWindow();
                                         }),
      PopUpMenu::ItemWithIconAndShortcut("Downloads...", download_icon,
                                         "Ctrl+J",
                                         []() {
                                           NETSURF_LOCK;
                                           ShowDownloadsWindow();
                                         }),
      PopUpMenu::ItemWithIconAndShortcut("Cookies...", cookie_icon, "",
                                         []() {
                                           NETSURF_LOCK;
                                           ShowCookieManagerWindow();
                                         }),
      PopUpMenu::Divider(),
      PopUpMenu::ItemWithIconAndShortcut("Save Page As...", save_icon, "Ctrl+S",
                                         []() {
                                           NETSURF_LOCK;
                                           SaveActivePage();
                                         }),
      PopUpMenu::ItemWithIconAndShortcut(
          "View Page Source", code_icon, "Ctrl+U",
          []() {
            NETSURF_LOCK;
            OpenDevTools(DevToolsPanel::Source);
          }),
      PopUpMenu::ItemWithIconAndShortcut(
          "Developer Tools", terminal_icon, "F12", []() {
            NETSURF_LOCK;
            ToggleDevTools(DevToolsPanel::Console);
          }));

  Point pos = anchor_node->GetAbsolutePosition();
  auto size = anchor_node->GetSize();
  PopUp::Show(anchor_node, Point{pos.x, pos.y + size.height}, menu);
}

void ShowWebContextMenu(struct gui_window* gw, const Point& click_point,
                        int doc_x, int doc_y) {
  if (!gw || !gw->GetBrowserWindow() || !gw->GetContentNode())
    return;
  struct browser_window* bw = gw->GetBrowserWindow();

  struct browser_window_features features{};
  browser_window_get_features(bw, doc_x, doc_y, &features);
  browser_editor_flags editor_flags = browser_window_get_editor_flags(bw);

  auto external_icon = Image::LoadImage(kExternalLinkIconPath);
  auto bookmark_plus_icon = Image::LoadImage(kBookmarkPlusIconPath);
  auto bookmark_icon = Image::LoadImage(kBookmarkIconPath);
  auto copy_icon = Image::LoadImage(kCopyIconPath);
  auto save_icon = Image::LoadImage(kSaveIconPath);
  auto search_icon = Image::LoadImage(kSearchIconPath);
  auto back_icon = Image::LoadImage(kBackIconPath);
  auto forward_icon = Image::LoadImage(kForwardIconPath);
  auto refresh_icon = Image::LoadImage(kRefreshIconPath);
  auto code_icon = Image::LoadImage(kCodeIconPath);
  auto terminal_icon = Image::LoadImage(kTerminalIconPath);

  auto menu = PopUpMenu::Container();

  if (features.link != nullptr) {
    std::shared_ptr<struct nsurl> link_url(
        nsurl_ref(features.link), [](struct nsurl* u) {
          if (u != nullptr)
            nsurl_unref(u);
        });
    std::string link_title =
        (features.link_title != nullptr && features.link_title_length > 0)
            ? std::string(features.link_title, features.link_title_length)
            : "";

    menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
        "Open Link in New Tab", external_icon, "", [bw, link_url]() {
          NETSURF_LOCK;
          struct browser_window* new_bw = nullptr;
          int flags = BW_CREATE_TAB | BW_CREATE_HISTORY;
          if (nsoption_bool(foreground_new))
            flags |= BW_CREATE_FOREGROUND;
          browser_window_create(
              static_cast<browser_window_create_flags>(flags), link_url.get(),
              browser_window_access_url(bw), bw, &new_bw);
        }));
    menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
        "Bookmark Link...", bookmark_plus_icon, "",
        [link_url, link_title]() {
          NETSURF_LOCK;
          AddBookmarkDialog(link_url.get(), link_title, GetGlobalUiWindow());
          UpdateBrowserToolbar();
        }));
    menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
        "Copy Link Address", copy_icon, "", [link_url]() {
          if (const char* s = nsurl_access(link_url.get()))
            ::perception::SetClipboard(std::string_view(s));
        }));
    menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
        "Save Link As...", save_icon, "", [bw, link_url]() {
          NETSURF_LOCK;
          browser_window_navigate(bw, link_url.get(),
                                  browser_window_access_url(bw),
                                  BW_NAVIGATE_DOWNLOAD, nullptr, nullptr,
                                  nullptr);
        }));
    menu->AddChild(PopUpMenu::Divider());
  }

  if (features.object != nullptr) {
    struct nsurl* raw_obj_url = hlcache_handle_get_url(features.object);
    if (raw_obj_url != nullptr) {
      std::shared_ptr<struct nsurl> obj_url(
          nsurl_ref(raw_obj_url), [](struct nsurl* u) {
            if (u != nullptr)
              nsurl_unref(u);
          });
      menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
          "Open Image in New Tab", external_icon, "", [bw, obj_url]() {
            NETSURF_LOCK;
            struct browser_window* new_bw = nullptr;
            int flags = BW_CREATE_TAB | BW_CREATE_HISTORY | BW_CREATE_FOREGROUND;
            browser_window_create(
                static_cast<browser_window_create_flags>(flags), obj_url.get(),
                browser_window_access_url(bw), bw, &new_bw);
          }));
      menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
          "Inspect Resource in DevTools", search_icon, "", [obj_url]() {
            NETSURF_LOCK;
            if (const char* s = nsurl_access(obj_url.get()))
              InspectResourceUrl(s);
          }));
      menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
          "Copy Image Address", copy_icon, "", [obj_url]() {
            if (const char* s = nsurl_access(obj_url.get()))
              ::perception::SetClipboard(std::string_view(s));
          }));
      menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
          "Save Image As...", save_icon, "", [bw, obj_url]() {
            NETSURF_LOCK;
            browser_window_navigate(bw, obj_url.get(),
                                    browser_window_access_url(bw),
                                    BW_NAVIGATE_DOWNLOAD, nullptr, nullptr,
                                    nullptr);
          }));
      menu->AddChild(PopUpMenu::Divider());
    }
  }

  bool can_back = browser_window_history_back_available(bw);
  bool can_forward = browser_window_history_forward_available(bw);
  menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
      "Back", back_icon, "Alt+Left",
      [bw]() {
        NETSURF_LOCK;
        if (browser_window_history_back_available(bw))
          browser_window_history_back(bw, false);
      },
      [can_back](Button& btn) { btn.SetEnabled(can_back); }));
  menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
      "Forward", forward_icon, "Alt+Right",
      [bw]() {
        NETSURF_LOCK;
        if (browser_window_history_forward_available(bw))
          browser_window_history_forward(bw, false);
      },
      [can_forward](Button& btn) { btn.SetEnabled(can_forward); }));
  menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
      "Reload", refresh_icon, "Ctrl+R", [bw]() {
        NETSURF_LOCK;
        browser_window_reload(bw, false);
      }));
  menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
      "Bookmark This Page...", bookmark_icon, "Ctrl+D", []() {
        NETSURF_LOCK;
        EditActivePageBookmarkDialog();
      }));

  menu->AddChild(PopUpMenu::Divider());

  if ((editor_flags & BW_EDITOR_CAN_CUT) != 0) {
    menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
        "Cut", nullptr, "Ctrl+X", [bw]() {
          NETSURF_LOCK;
          browser_window_key_press(bw, NS_KEY_CUT_SELECTION);
        }));
  }
  menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
      "Copy", copy_icon, "Ctrl+C", [bw]() {
        NETSURF_LOCK;
        browser_window_key_press(bw, NS_KEY_COPY_SELECTION);
      }));
  if ((editor_flags & BW_EDITOR_CAN_PASTE) != 0) {
    menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
        "Paste", nullptr, "Ctrl+V", [bw]() {
          NETSURF_LOCK;
          browser_window_key_press(bw, NS_KEY_PASTE);
        }));
  }
  menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
      "Select All", nullptr, "Ctrl+A", [bw]() {
        NETSURF_LOCK;
        browser_window_key_press(bw, NS_KEY_SELECT_ALL);
      }));

  menu->AddChild(PopUpMenu::Divider());

  menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
      "Save Page As...", save_icon, "Ctrl+S", []() {
        NETSURF_LOCK;
        SaveActivePage();
      }));
  menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
      "View Page Source", code_icon, "Ctrl+U", []() {
        NETSURF_LOCK;
        OpenDevTools(DevToolsPanel::Source);
      }));
  menu->AddChild(PopUpMenu::ItemWithIconAndShortcut(
      "Inspect / Developer Tools", terminal_icon, "F12", []() {
        NETSURF_LOCK;
        OpenDevTools(DevToolsPanel::Console);
      }));

  Point node_pos = gw->GetContentNode()->GetAbsolutePosition();
  Point anchor{node_pos.x + click_point.x, node_pos.y + click_point.y};
  PopUp::Show(gw->GetContentNode(), anchor, menu);
}

void SyncDockedDevToolsSplit() {
  bool show_docked = IsDevToolsOpen() && IsDevToolsDocked();
  if (auto docked_node = GetDockedDevToolsNode()) {
    docked_node->GetLayout().SetDisplay(show_docked ? YGDisplayFlex
                                                    : YGDisplayNone);
  }
  if (g_viewport_devtools_split) {
    const auto& children = g_viewport_devtools_split->GetChildren();
    if (children.size() >= 3) {
      auto it = children.begin();
      ++it;
      if (*it) {
        (*it)->GetLayout().SetDisplay(show_docked ? YGDisplayFlex
                                                  : YGDisplayNone);
      }
    }
  }
  if (GetActiveTab() && GetActiveTab()->GetBrowserWindow()) {
    browser_window_schedule_reformat(GetActiveTab()->GetBrowserWindow());
    if (GetActiveTab()->GetContentNode())
      GetActiveTab()->GetContentNode()->Invalidate();
  }
  if (GetGlobalUiWindow())
    GetGlobalUiWindow()->Invalidate();
}

}  // namespace

FiberRecursiveMutex& GetNetSurfMutex() {
  static FiberRecursiveMutex mutex;
  return mutex;
}

void UpdateBrowserToolbar() {
  Window* active = GetActiveTab();
  struct browser_window* bw = active ? active->GetBrowserWindow() : nullptr;

  if (g_back_button) {
    g_back_button->SetEnabled(bw != nullptr &&
                              browser_window_history_back_available(bw));
  }
  if (g_forward_button) {
    g_forward_button->SetEnabled(bw != nullptr &&
                                 browser_window_history_forward_available(bw));
  }

  bool loading = active != nullptr && active->IsThrobberRunning();
  if (g_reload_stop_image_button) {
    g_reload_stop_image_button->SetImage(
        Image::LoadImage(loading ? kCloseIconPath : kRefreshIconPath));
  }
  if (g_reload_stop_button_node) {
    Tooltip::Attach(g_reload_stop_button_node,
                    loading ? "Stop Loading (Esc)" : "Reload (Ctrl+R)");
  }
  if (g_loading_indicator_node) {
    YGDisplay desired = loading ? YGDisplayFlex : YGDisplayNone;
    if (g_loading_indicator_node->GetLayout().GetDisplay() != desired)
      g_loading_indicator_node->GetLayout().SetDisplay(desired);
  }

  if (g_security_image_button) {
    browser_window_page_info_state state =
        bw ? browser_window_get_page_info_state(bw) : PAGE_STATE_UNKNOWN;
    std::string_view icon_path = kSearchIconPath;
    uint32_t tint = kDefaultSecurityIconTint;
    switch (state) {
      case PAGE_STATE_SECURE:
        icon_path = kPageInfoSecureIconPath;
        tint = kSecureIconTint;
        break;
      case PAGE_STATE_INSECURE:
        icon_path = kPageInfoInsecureIconPath;
        tint = kInsecureIconTint;
        break;
      case PAGE_STATE_SECURE_OVERRIDE:
      case PAGE_STATE_SECURE_ISSUES:
        icon_path = kPageInfoWarningIconPath;
        tint = kWarningIconTint;
        break;
      case PAGE_STATE_LOCAL:
        icon_path = kPageInfoLocalIconPath;
        tint = kDefaultSecurityIconTint;
        break;
      case PAGE_STATE_INTERNAL:
        icon_path = kPageInfoInternalIconPath;
        tint = kDefaultSecurityIconTint;
        break;
      default:
        icon_path = kSearchIconPath;
        tint = kDefaultSecurityIconTint;
        break;
    }
    g_security_image_button->SetImage(Image::LoadImage(icon_path));
    g_security_image_button->SetColor(tint);
  }

  if (g_bookmark_star_image_button) {
    bool bookmarked = false;
    if (bw != nullptr) {
      struct nsurl* url = nullptr;
      if (browser_window_get_url(bw, true, &url) == NSERROR_OK &&
          url != nullptr) {
        bookmarked = IsUrlBookmarked(url);
        nsurl_unref(url);
      }
    }
    g_bookmark_star_image_button->SetImage(
        Image::LoadImage(bookmarked ? kStarFilledIconPath : kStarIconPath));
    if (bookmarked)
      g_bookmark_star_image_button->SetColor(kBookmarkedStarTint);
    else
      g_bookmark_star_image_button->ClearColor();
  }
}

void UpdateStatusBarBadges() {
  if (g_download_badge_node && g_download_badge_label) {
    DownloadSummary summary = GetDownloadSummary();
    if (summary.total_count <= 0) {
      if (g_download_badge_node->GetLayout().GetDisplay() != YGDisplayNone)
        g_download_badge_node->GetLayout().SetDisplay(YGDisplayNone);
    } else {
      if (g_download_badge_node->GetLayout().GetDisplay() != YGDisplayFlex)
        g_download_badge_node->GetLayout().SetDisplay(YGDisplayFlex);
      std::string text;
      if (summary.active_count > 0) {
        int pct = summary.average_percent;
        if (pct <= 0 && summary.active_total_bytes > 0) {
          pct = static_cast<int>((summary.active_received_bytes * 100) /
                                 summary.active_total_bytes);
        }
        text = std::to_string(summary.active_count) + " downloading";
        if (summary.active_total_bytes > 0 || pct > 0)
          text += " (" + std::to_string(pct) + "%)";
      } else {
        text = std::to_string(summary.total_count) + " downloaded";
      }
      g_download_badge_label->SetText(text);
    }
  }

  if (g_console_badge_image_view && g_console_badge_label) {
    ConsoleCounts counts = GetConsoleCountsForTab(GetActiveTab());
    if (counts.error_count > 0) {
      g_console_badge_image_view->SetImage(
          Image::LoadImage(kAlertCircleIconPath));
      std::string label = std::to_string(counts.error_count) +
                          (counts.error_count == 1 ? " error" : " errors");
      if (counts.warning_count > 0) {
        label += ", " + std::to_string(counts.warning_count) +
                 (counts.warning_count == 1 ? " warning" : " warnings");
      }
      g_console_badge_label->SetText(label);
      g_console_badge_label->SetColor(kInsecureIconTint);
    } else if (counts.warning_count > 0) {
      g_console_badge_image_view->SetImage(
          Image::LoadImage(kAlertTriangleIconPath));
      g_console_badge_label->SetText(
          std::to_string(counts.warning_count) +
          (counts.warning_count == 1 ? " warning" : " warnings"));
      g_console_badge_label->SetColor(kWarningIconTint);
    } else {
      g_console_badge_image_view->SetImage(Image::LoadImage(kTerminalIconPath));
      g_console_badge_label->SetText("DevTools");
      g_console_badge_label->SetColor(::perception::ui::kSecondaryTextColor);
    }
  }
}

Window::Window() : is_alive_(std::make_shared<bool>(true)) {}

Window::~Window() {
  if (is_alive_)
    *is_alive_ = false;
}

void Window::PlaceCaret(int x, int y, int height, const struct rect* clip) {
  has_caret_ = true;
  caret_x_ = x;
  caret_y_ = y;
  caret_height_ = height;
  if (clip != nullptr) {
    has_caret_clip_ = true;
    caret_clip_ = *clip;
  } else {
    has_caret_clip_ = false;
  }
}

void Window::RemoveCaret() {
  has_caret_ = false;
  has_caret_clip_ = false;
}

browser_mouse_state Window::GetMouseModifiers() const {
  int mods = 0;
  if (shift_pressed_)
    mods |= BROWSER_MOUSE_MOD_1;
  if (ctrl_pressed_)
    mods |= BROWSER_MOUSE_MOD_2;
  if (alt_pressed_)
    mods |= BROWSER_MOUSE_MOD_3;
  return static_cast<browser_mouse_state>(mods);
}

browser_mouse_state Window::RecordClickAndGetMultiClickFlags(int doc_x,
                                                             int doc_y) {
  auto now = std::chrono::steady_clock::now();
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - last_click_time_)
                        .count();
  if (click_count_ > 0 && elapsed_ms <= kDoubleClickTimeMs &&
      std::abs(doc_x - last_click_doc_x_) <= kDragThresholdPixels &&
      std::abs(doc_y - last_click_doc_y_) <= kDragThresholdPixels) {
    click_count_ = (click_count_ % 3) + 1;
  } else {
    click_count_ = 1;
  }
  last_click_time_ = now;
  last_click_doc_x_ = doc_x;
  last_click_doc_y_ = doc_y;

  if (click_count_ == 2)
    return BROWSER_MOUSE_DOUBLE_CLICK;
  if (click_count_ == 3)
    return BROWSER_MOUSE_TRIPLE_CLICK;
  return static_cast<browser_mouse_state>(0);
}

void Window::ScrollViewportBy(float dx, float dy) {
  if (!bw_ || !content_node_)
    return;
  int raw_w = 0;
  int raw_h = 0;
  browser_window_get_extents(bw_, true, &raw_w, &raw_h);
  float vp_w = std::max(1.0f, content_node_->GetSize().width);
  float vp_h = std::max(1.0f, content_node_->GetSize().height);
  float max_x = std::max(0.0f, static_cast<float>(raw_w) - vp_w);
  float max_y = std::max(0.0f, static_cast<float>(raw_h) - vp_h);

  scroll_.x = std::clamp(scroll_.x + dx, 0.0f, max_x);
  scroll_.y = std::clamp(scroll_.y + dy, 0.0f, max_y);
  pending_scroll_ = scroll_;
  UpdateScrollBars(this);
  content_node_->Invalidate();
}

void Window::ScrollViewportToY(float y) {
  if (!bw_ || !content_node_)
    return;
  int raw_w = 0;
  int raw_h = 0;
  browser_window_get_extents(bw_, true, &raw_w, &raw_h);
  float vp_h = std::max(1.0f, content_node_->GetSize().height);
  float max_y = std::max(0.0f, static_cast<float>(raw_h) - vp_h);

  scroll_.y = std::clamp(y, 0.0f, max_y);
  pending_scroll_ = scroll_;
  UpdateScrollBars(this);
  content_node_->Invalidate();
}

void Window::HandleKeyDown(
    const ::perception::window::KeyboardKeyEvent& event) {
  using ::perception::ui::IsControlKey;
  using ::perception::ui::IsShiftKey;
  using ::perception::ui::KeyCode;
  using ::perception::ui::ScancodeToAscii;

  if (IsShiftKey(event.key)) {
    shift_pressed_ = true;
    return;
  }
  if (IsControlKey(event.key)) {
    ctrl_pressed_ = true;
    return;
  }
  KeyCode key = static_cast<KeyCode>(event.key);
  if (key == KeyCode::LeftAlt) {
    alt_pressed_ = true;
    return;
  }

  if (ctrl_pressed_) {
    if (shift_pressed_ && (key == KeyCode::I || key == KeyCode::J)) {
      ToggleDevTools(DevToolsPanel::Console);
      return;
    }
    switch (key) {
      case KeyCode::L:
        if (auto* input = GetGlobalUrlInput()) {
          input->Focus();
          input->SelectAll();
        }
        return;
      case KeyCode::T:
        OpenNewTab();
        return;
      case KeyCode::W:
        CloseTab(GetActiveTabIndex());
        return;
      case KeyCode::R:
        browser_window_reload(bw_, shift_pressed_);
        return;
      case KeyCode::F:
        ShowFindBar();
        return;
      case KeyCode::D:
        ToggleActivePageBookmark();
        return;
      case KeyCode::B:
        ShowBookmarksWindow();
        return;
      case KeyCode::H:
        ShowGlobalHistoryWindow();
        return;
      case KeyCode::J:
        ShowDownloadsWindow();
        return;
      case KeyCode::U:
        OpenDevTools(DevToolsPanel::Source);
        return;
      case KeyCode::S:
        SaveActivePage();
        return;
      case KeyCode::Equals:
      case KeyCode::KeypadPlus:
        AdjustZoom(kZoomStep);
        return;
      case KeyCode::Hyphen:
      case KeyCode::KeypadMinus:
        AdjustZoom(-kZoomStep);
        return;
      case KeyCode::Zero:
        ResetZoom();
        return;
      case KeyCode::A:
        browser_window_key_press(bw_, NS_KEY_SELECT_ALL);
        return;
      case KeyCode::C:
        browser_window_key_press(bw_, NS_KEY_COPY_SELECTION);
        return;
      case KeyCode::V:
        browser_window_key_press(bw_, NS_KEY_PASTE);
        return;
      case KeyCode::X:
        browser_window_key_press(bw_, NS_KEY_CUT_SELECTION);
        return;
      case KeyCode::Z:
        browser_window_key_press(bw_,
                                 shift_pressed_ ? NS_KEY_REDO : NS_KEY_UNDO);
        return;
      case KeyCode::Y:
        browser_window_key_press(bw_, NS_KEY_REDO);
        return;
      default:
        break;
    }
  }

  if (alt_pressed_) {
    if (key == KeyCode::LeftArrow) {
      if (browser_window_history_back_available(bw_))
        browser_window_history_back(bw_, false);
      return;
    }
    if (key == KeyCode::RightArrow) {
      if (browser_window_history_forward_available(bw_))
        browser_window_history_forward(bw_, false);
      return;
    }
    if (key == KeyCode::UpArrow) {
      if (browser_window_up_available(bw_))
        browser_window_navigate_up(bw_, false);
      return;
    }
  }

  if (key == KeyCode::F5) {
    browser_window_reload(bw_, shift_pressed_);
    return;
  }
  if (key == KeyCode::F12) {
    ToggleDevTools(DevToolsPanel::Console);
    return;
  }

  float vp_h = content_node_
                   ? std::max(kLineScrollStep,
                              content_node_->GetSize().height *
                                  kPageScrollFraction)
                   : 300.0f;

  switch (key) {
    case KeyCode::Backspace:
      browser_window_key_press(bw_, NS_KEY_DELETE_LEFT);
      return;
    case KeyCode::Delete:
      browser_window_key_press(bw_, NS_KEY_DELETE_RIGHT);
      return;
    case KeyCode::Enter:
      browser_window_key_press(bw_, NS_KEY_CR);
      return;
    case KeyCode::Tab:
      browser_window_key_press(
          bw_, shift_pressed_ ? NS_KEY_SHIFT_TAB : NS_KEY_TAB);
      return;
    case KeyCode::LeftArrow: {
      uint32_t nskey = ctrl_pressed_
                           ? NS_KEY_LINE_START
                           : (shift_pressed_ ? NS_KEY_WORD_LEFT : NS_KEY_LEFT);
      if (!browser_window_key_press(bw_, nskey) && !ctrl_pressed_ &&
          !shift_pressed_) {
        ScrollViewportBy(-kLineScrollStep, 0.0f);
      }
      return;
    }
    case KeyCode::RightArrow: {
      uint32_t nskey =
          ctrl_pressed_ ? NS_KEY_LINE_END
                        : (shift_pressed_ ? NS_KEY_WORD_RIGHT : NS_KEY_RIGHT);
      if (!browser_window_key_press(bw_, nskey) && !ctrl_pressed_ &&
          !shift_pressed_) {
        ScrollViewportBy(kLineScrollStep, 0.0f);
      }
      return;
    }
    case KeyCode::UpArrow:
      if (!browser_window_key_press(bw_, NS_KEY_UP))
        ScrollViewportBy(0.0f, -kLineScrollStep);
      return;
    case KeyCode::DownArrow:
      if (!browser_window_key_press(bw_, NS_KEY_DOWN))
        ScrollViewportBy(0.0f, kLineScrollStep);
      return;
    case KeyCode::Home:
      if (!browser_window_key_press(
              bw_, ctrl_pressed_ ? NS_KEY_TEXT_START : NS_KEY_LINE_START)) {
        ScrollViewportToY(0.0f);
      }
      return;
    case KeyCode::End:
      if (!browser_window_key_press(
              bw_, ctrl_pressed_ ? NS_KEY_TEXT_END : NS_KEY_LINE_END)) {
        ScrollViewportToY(1e9f);
      }
      return;
    case KeyCode::PageUp:
      if (!browser_window_key_press(bw_, NS_KEY_PAGE_UP))
        ScrollViewportBy(0.0f, -vp_h);
      return;
    case KeyCode::PageDown:
      if (!browser_window_key_press(bw_, NS_KEY_PAGE_DOWN))
        ScrollViewportBy(0.0f, vp_h);
      return;
    case KeyCode::Space:
      if (!browser_window_key_press(bw_, ' '))
        ScrollViewportBy(0.0f, shift_pressed_ ? -vp_h : vp_h);
      return;
    case KeyCode::Escape:
      if (g_find_bar_open) {
        CloseFindBar();
      } else if (throbber_running_) {
        browser_window_stop(bw_);
      } else {
        browser_window_key_press(bw_, NS_KEY_ESCAPE);
      }
      return;
    default:
      break;
  }

  char ascii = ScancodeToAscii(event.key, shift_pressed_);
  if (ascii != '\0') {
    browser_window_key_press(
        bw_, static_cast<uint32_t>(static_cast<unsigned char>(ascii)));
  }
}

void Window::HandleKeyUp(
    const ::perception::window::KeyboardKeyEvent& event) {
  using ::perception::ui::IsControlKey;
  using ::perception::ui::IsShiftKey;
  using ::perception::ui::KeyCode;

  if (IsShiftKey(event.key))
    shift_pressed_ = false;
  else if (IsControlKey(event.key))
    ctrl_pressed_ = false;
  else if (static_cast<KeyCode>(event.key) == KeyCode::LeftAlt)
    alt_pressed_ = false;
}

namespace {

::perception::window::Cursor MapPointerShape(gui_pointer_shape shape) {
  switch (shape) {
    case GUI_POINTER_POINT:
      return ::perception::window::Cursor::Poke;
    case GUI_POINTER_CARET:
      return ::perception::window::Cursor::Caret;
    case GUI_POINTER_MOVE:
      return ::perception::window::Cursor::Drag;
    case GUI_POINTER_UP:
    case GUI_POINTER_DOWN:
      return ::perception::window::Cursor::ResizeVertical;
    case GUI_POINTER_LEFT:
    case GUI_POINTER_RIGHT:
      return ::perception::window::Cursor::ResizeHorizontal;
    case GUI_POINTER_RU:
    case GUI_POINTER_LD:
      return ::perception::window::Cursor::ResizeDiagonalTopRightBottomLeft;
    case GUI_POINTER_LU:
    case GUI_POINTER_RD:
      return ::perception::window::Cursor::ResizeDiagonalTopLeftBottomRight;
    default:
      return ::perception::window::Cursor::Pointer;
  }
}

static struct gui_window* gui_window_create(struct browser_window* bw,
                                            struct gui_window* existing,
                                            gui_window_create_flags flags);

static void gui_window_destroy(struct gui_window* gw);

nserror FbWindowInvalidateArea(struct gui_window* g, const struct rect* rect) {
  if (g && g->GetContentNode())
    g->GetContentNode()->Invalidate();
  return NSERROR_OK;
}

bool GuiWindowGetScroll(struct gui_window* g, int* sx, int* sy) {
  NETSURF_LOCK;
  *sx = g ? static_cast<int>(g->GetScroll().x) : 0;
  *sy = g ? static_cast<int>(g->GetScroll().y) : 0;
  return true;
}

nserror GuiWindowSetScroll(struct gui_window* gw, const struct rect* rect) {
  NETSURF_LOCK;
  if (gw && rect) {
    gw->GetScroll().x = static_cast<float>(rect->x0);
    gw->GetScroll().y = static_cast<float>(rect->y0);
    gw->GetPendingScroll() = gw->GetScroll();
    if (gw->GetContentNode())
      gw->GetContentNode()->Invalidate();
    UpdateScrollBars(gw);
  }
  return NSERROR_OK;
}

nserror GuiWindowGetDimensions(struct gui_window* gw, int* width,
                               int* height) {
  NETSURF_LOCK;
  if (gw && gw->GetContentNode()) {
    *width = static_cast<int>(gw->GetContentNode()->GetSize().width);
    *height = static_cast<int>(gw->GetContentNode()->GetSize().height);
    if (*width <= 0)
      *width = kFallbackViewportWidth;
    if (*height <= 0)
      *height = kFallbackViewportHeight;
    if (*width < kMinViewportDimension)
      *width = kMinViewportDimension;
    if (*height < kMinViewportDimension)
      *height = kMinViewportDimension;
    gw->SetLastFormatWidth(*width);
    gw->SetLastFormatHeight(*height);
  } else {
    *width = kFallbackViewportWidth;
    *height = kFallbackViewportHeight;
  }
  return NSERROR_OK;
}

nserror GuiWindowEvent(struct gui_window* gw, enum gui_window_event event) {
  NETSURF_LOCK;
  if (!gw)
    return NSERROR_OK;

  switch (event) {
    case GW_EVENT_REMOVE_CARET:
      gw->RemoveCaret();
      if (gw->GetContentNode())
        gw->GetContentNode()->Invalidate();
      break;

    case GW_EVENT_START_THROBBER:
      gw->SetThrobberRunning(true);
      NotifyDevToolsNavigationStarted(gw);
      if (gw == GetActiveTab()) {
        UpdateBrowserToolbar();
        UpdateStatusBarBadges();
      }
      break;

    case GW_EVENT_STOP_THROBBER:
      gw->SetThrobberRunning(false);
      NotifyDevToolsPageLoaded(gw);
      SavePersistentData();
      UpdateTabBar();
      if (gw == GetActiveTab()) {
        UpdateBrowserToolbar();
        UpdateStatusBarBadges();
      }
      break;

    case GW_EVENT_PAGE_INFO_CHANGE:
      if (gw == GetActiveTab())
        UpdateBrowserToolbar();
      break;

    case GW_EVENT_NEW_CONTENT:
      if (gw == GetActiveTab())
        UpdateBrowserToolbar();
      [[fallthrough]];
    case GW_EVENT_UPDATE_EXTENT:
      if (!gw->GetExtentDeferred()) {
        gw->GetExtentDeferred() = true;
        auto is_alive = gw->GetIsAlive();
        ::perception::DeferAfterEvents([gw, is_alive]() {
          if (!*is_alive)
            return;
          NETSURF_LOCK;
          UpdateScrollBars(gw);
          if (gw->GetContentNode())
            gw->GetContentNode()->Invalidate();
          gw->GetExtentDeferred() = false;
        });
      }
      break;

    default:
      break;
  }
  return NSERROR_OK;
}

nserror GuiWindowSetUrl(struct gui_window* g, nsurl* url) {
  if (g && g == GetActiveTab()) {
    const char* url_str = url ? nsurl_access(url) : "";
    auto* url_input = GetGlobalUrlInput();
    if (url_input && !url_input->HasFocus()) {
      g_suppress_url_suggestions = true;
      url_input->SetText(url_str ? url_str : "");
      g_suppress_url_suggestions = false;
    }
    UpdateBrowserToolbar();
  }
  return NSERROR_OK;
}

void GuiWindowSetIcon(struct gui_window* gw, struct hlcache_handle* icon) {
  if (!gw)
    return;
  struct bitmap* bmp = icon ? content_get_bitmap(icon) : nullptr;
  if (bmp != nullptr) {
    if (!bmp->cached_image && !bmp->sk_bitmap.drawsNothing())
      bmp->cached_image = bmp->sk_bitmap.asImage();
    if (bmp->cached_image)
      gw->SetFavicon(Image::FromSkImage(bmp->cached_image));
  }
  UpdateTabBar();
  if (GetGlobalUiWindow())
    GetGlobalUiWindow()->Invalidate();
}

void GuiWindowSetStatus(struct gui_window* g, const char* text) {
  auto* status_label = GetGlobalStatusLabel();
  if (g && g == GetActiveTab() && status_label)
    status_label->SetText(text ? text : "");
}

void GuiWindowSetPointer(struct gui_window* g, gui_pointer_shape shape) {
  if (g && g->GetContentNode())
    g->GetContentNode()->SetCursor(MapPointerShape(shape));
}

void GuiWindowPlaceCaret(struct gui_window* g, int x, int y, int height,
                         const struct rect* clip) {
  if (!g)
    return;
  g->PlaceCaret(x, y, height, clip);
  if (g->GetContentNode())
    g->GetContentNode()->Invalidate();
}

void GuiWindowSetTitle(struct gui_window* gw, const char* title) {
  if (gw) {
    gw->GetTitle() = title ? title : "";
    if (!gw->GetTitleDeferred()) {
      gw->GetTitleDeferred() = true;
      auto is_alive = gw->GetIsAlive();
      ::perception::DeferAfterEvents([gw, is_alive]() {
        if (!*is_alive)
          return;
        NETSURF_LOCK;
        UpdateTabBar();
        if (GetGlobalUiWindow())
          GetGlobalUiWindow()->Invalidate();
        gw->GetTitleDeferred() = false;
      });
    }
  }
}

nserror GuiWindowSaveLink(struct gui_window* g, struct nsurl* url,
                          const char* /*title*/) {
  if (!g || !g->GetBrowserWindow() || !url)
    return NSERROR_BAD_PARAMETER;
  return browser_window_navigate(
      g->GetBrowserWindow(), url,
      browser_window_access_url(g->GetBrowserWindow()), BW_NAVIGATE_DOWNLOAD,
      nullptr, nullptr, nullptr);
}

void GuiWindowFileGadgetOpen(struct gui_window* gw,
                             struct hlcache_handle* /*hl*/,
                             struct form_control* gadget) {
  if (!gw || !gadget)
    return;
  auto is_alive = gw->GetIsAlive();
  ShowOpenFileDialog(
      [gw, is_alive, gadget](bool succeeded, std::string_view path) {
        if (!succeeded || path.empty() || !*is_alive)
          return;
        std::string path_str(path);
        NETSURF_LOCK;
        if (gw->GetBrowserWindow()) {
          browser_window_set_gadget_filename(gw->GetBrowserWindow(), gadget,
                                             path_str.c_str());
        }
      },
      {}, "", "Select File to Upload", GetGlobalUiWindow());
}

void GuiWindowConsoleLog(struct gui_window* gw,
                         browser_window_console_source src, const char* msg,
                         size_t msglen, browser_window_console_flags flags) {
  AppendConsoleMessage(gw, src, msg, msglen, flags);
  if (gw == GetActiveTab())
    UpdateStatusBarBadges();
}

void SearchStatus(bool found, void* /*p*/) {
  if (!g_find_status_label)
    return;
  g_find_status_label->SetText(found ? "Found" : "Not found");
  g_find_status_label->SetColor(found ? ::perception::ui::kSuccessTextColor
                                      : ::perception::ui::kWarningTextColor);
}

void SearchHourglass(bool /*active*/, void* /*p*/) {}

void SearchAddRecent(const char* /*string*/, void* /*p*/) {}

void SearchForwardState(bool active, void* /*p*/) {
  if (g_find_next_button)
    g_find_next_button->SetEnabled(active);
}

void SearchBackState(bool active, void* /*p*/) {
  if (g_find_prev_button)
    g_find_prev_button->SetEnabled(active);
}

void FlushPendingMouseHover(struct gui_window* gw) {
  if (gw && gw->GetHasPendingHover()) {
    browser_window_mouse_track(
        gw->GetBrowserWindow(), gw->GetPendingHoverState(),
        static_cast<int>(gw->GetPendingHover().x),
        static_cast<int>(gw->GetPendingHover().y));
    gw->GetHasPendingHover() = false;
  }
}

static struct gui_window* gui_window_create(struct browser_window* bw,
                                            struct gui_window* /*existing*/,
                                            gui_window_create_flags flags) {
  struct gui_window* gw = new gui_window();
  gw->GetBrowserWindow() = bw;
  gw->GetScroll() = {.x = 0.0f, .y = 0.0f};
  gw->GetTitle() = "New Tab";

  // Create Content Viewport Node for this tab.
  gw->GetContentNode() = Node::Empty([](Layout& layout) {
    layout.SetFlexGrow(1.0f);
    layout.SetAlignSelf(YGAlignStretch);
  });

  auto h_scroll_bar_node = ScrollBar::HorizontalScrollBar(
      &gw->GetHorizontalScrollBar(),
      [](Layout& layout) { layout.SetAlignSelf(YGAlignStretch); });
  auto v_scroll_bar_node = ScrollBar::VerticalScrollBar(
      &gw->GetVerticalScrollBar(),
      [](Layout& layout) { layout.SetAlignSelf(YGAlignStretch); });

  gw->GetHorizontalScrollBarNode() = h_scroll_bar_node;
  gw->GetVerticalScrollBarNode() = v_scroll_bar_node;

  gw->GetTabRootNode() = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetAlignSelf(YGAlignStretch);
        layout.SetGap(0.0f);
      },
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
            layout.SetAlignSelf(YGAlignStretch);
            layout.SetGap(0.0f);
          },
          gw->GetContentNode(), h_scroll_bar_node),
      v_scroll_bar_node);

  gw->GetHorizontalScrollBar()->OnScroll([gw](float value) {
    if (value == gw->GetPendingScroll().x)
      return;
    gw->GetPendingScroll().x = value;
    gw->GetHasPendingScroll() = true;

    if (!gw->GetScrollDeferred()) {
      gw->GetScrollDeferred() = true;
      auto is_alive = gw->GetIsAlive();
      ::perception::DeferAfterEvents([gw, is_alive]() {
        if (!*is_alive)
          return;
        if (gw->GetHasPendingScroll()) {
          gw->GetScroll() = gw->GetPendingScroll();
          gw->GetContentNode()->Invalidate();
          gw->GetHasPendingScroll() = false;
        }
        gw->GetScrollDeferred() = false;
      });
    }
  });

  gw->GetVerticalScrollBar()->OnScroll([gw](float value) {
    if (value == gw->GetPendingScroll().y)
      return;
    gw->GetPendingScroll().y = value;
    gw->GetHasPendingScroll() = true;

    if (!gw->GetScrollDeferred()) {
      gw->GetScrollDeferred() = true;
      auto is_alive = gw->GetIsAlive();
      ::perception::DeferAfterEvents([gw, is_alive]() {
        if (!*is_alive)
          return;
        if (gw->GetHasPendingScroll()) {
          gw->GetScroll() = gw->GetPendingScroll();
          gw->GetContentNode()->Invalidate();
          gw->GetHasPendingScroll() = false;
        }
        gw->GetScrollDeferred() = false;
      });
    }
  });

  // Wire up Focus and Keyboard interaction.
  auto focusable =
      gw->GetContentNode()->GetOrAdd<::perception::ui::components::Focusable>();
  focusable->OnUnfocus([gw]() {
    gw->SetShiftPressed(false);
    gw->SetCtrlPressed(false);
    gw->SetAltPressed(false);
  });
  focusable->OnKeyDown(
      [gw](const ::perception::window::KeyboardKeyEvent& event) {
        NETSURF_LOCK;
        gw->HandleKeyDown(event);
      });
  focusable->OnKeyUp([gw](const ::perception::window::KeyboardKeyEvent& event) {
    NETSURF_LOCK;
    gw->HandleKeyUp(event);
  });

  // Wire up Draw callback using Skia Canvas.
  gw->GetContentNode()->OnDraw([gw](const DrawContext& context) {
    NETSURF_LOCK;
    if (!context.skia_canvas)
      return;

    if (!browser_window_has_content(gw->GetBrowserWindow())) {
      SkPaint paint;
      paint.setColor(SkColorSetARGB(255, 255, 255, 255));
      context.skia_canvas->drawRect(
          SkRect::MakeXYWH(context.area.origin.x, context.area.origin.y,
                           context.area.size.width, context.area.size.height),
          paint);
      return;
    }

    if (!browser_window_redraw_ready(gw->GetBrowserWindow()))
      return;

    int width = static_cast<int>(context.area.size.width);
    int height = static_cast<int>(context.area.size.height);

    if (width < kMinViewportDimension)
      width = kMinViewportDimension;
    if (height < kMinViewportDimension)
      height = kMinViewportDimension;

    if (gw->GetLastFormatWidth() != width ||
        gw->GetLastFormatHeight() != height) {
      gw->SetLastFormatWidth(width);
      gw->SetLastFormatHeight(height);
      browser_window_reformat(gw->GetBrowserWindow(), false, width, height);
    }

    int initial_save = context.skia_canvas->save();
    context.skia_canvas->translate(context.area.origin.x - gw->GetScroll().x,
                                   context.area.origin.y - gw->GetScroll().y);

    SetActiveCanvas(context.skia_canvas);

    struct redraw_context ctx = {
        .interactive = true, .background_images = true, .plot = &skia_plotters};

    struct rect rect = {.x0 = static_cast<int>(gw->GetScroll().x),
                        .y0 = static_cast<int>(gw->GetScroll().y),
                        .x1 = static_cast<int>(gw->GetScroll().x) + width,
                        .y1 = static_cast<int>(gw->GetScroll().y) + height};

    browser_window_redraw(gw->GetBrowserWindow(), 0, 0, &rect, &ctx);

    SetActiveCanvas(nullptr);

    if (gw->HasCaret()) {
      int caret_x = gw->GetCaretX();
      int caret_y = gw->GetCaretY();
      int caret_h = gw->GetCaretHeight();
      if (caret_h <= 0)
        caret_h = kDefaultCaretHeight;

      bool visible = true;
      if (gw->HasCaretClip()) {
        const auto& clip = gw->GetCaretClip();
        if (caret_x < clip.x0 || caret_x >= clip.x1 ||
            caret_y + caret_h <= clip.y0 || caret_y >= clip.y1) {
          visible = false;
        }
      }

      if (visible) {
        SkPaint caret_paint;
        caret_paint.setColor(SK_ColorBLACK);
        caret_paint.setStrokeWidth(1.0f);
        caret_paint.setStyle(SkPaint::kStroke_Style);
        context.skia_canvas->drawLine(
            static_cast<float>(caret_x), static_cast<float>(caret_y),
            static_cast<float>(caret_x), static_cast<float>(caret_y + caret_h),
            caret_paint);
      }
    }

    context.skia_canvas->restoreToCount(initial_save);
  });

  // Wire up Mouse interaction.
  gw->GetContentNode()->OnMouseHover([gw](const Point& p) {
    NETSURF_LOCK;
    int doc_x = static_cast<int>(p.x) + static_cast<int>(gw->GetScroll().x);
    int doc_y = static_cast<int>(p.y) + static_cast<int>(gw->GetScroll().y);
    browser_mouse_state mods = gw->GetMouseModifiers();

    if (gw->IsMouseDown()) {
      if (!gw->HasDragStarted()) {
        if (std::abs(doc_x - gw->GetPressDocX()) > kDragThresholdPixels ||
            std::abs(doc_y - gw->GetPressDocY()) > kDragThresholdPixels) {
          browser_mouse_state drag_btn =
              (gw->GetMouseButtonDown() ==
               ::perception::window::MouseButton::Middle)
                  ? BROWSER_MOUSE_DRAG_2
                  : BROWSER_MOUSE_DRAG_1;
          browser_window_mouse_click(
              gw->GetBrowserWindow(),
              static_cast<browser_mouse_state>(drag_btn | mods),
              gw->GetPressDocX(), gw->GetPressDocY());
          gw->SetDragStarted(true);
        }
      }
      if (gw->HasDragStarted()) {
        int mouse = BROWSER_MOUSE_DRAG_ON | mods;
        if (gw->GetMouseButtonDown() ==
            ::perception::window::MouseButton::Middle) {
          mouse |= BROWSER_MOUSE_HOLDING_2;
        } else {
          mouse |= BROWSER_MOUSE_HOLDING_1;
        }
        browser_window_mouse_track(
            gw->GetBrowserWindow(),
            static_cast<browser_mouse_state>(mouse), doc_x, doc_y);
      }
      return;
    }

    gw->GetPendingHover() = {.x = static_cast<float>(doc_x),
                             .y = static_cast<float>(doc_y)};
    gw->GetPendingHoverState() = mods;
    gw->GetHasPendingHover() = true;

    if (!gw->GetHoverDeferred()) {
      gw->GetHoverDeferred() = true;
      auto is_alive = gw->GetIsAlive();
      ::perception::DeferAfterEvents([gw, is_alive]() {
        if (!*is_alive)
          return;
        if (gw->GetHasPendingHover()) {
          NETSURF_LOCK;
          browser_window_mouse_track(
              gw->GetBrowserWindow(), gw->GetPendingHoverState(),
              static_cast<int>(gw->GetPendingHover().x),
              static_cast<int>(gw->GetPendingHover().y));
          gw->GetHasPendingHover() = false;
        }
        gw->GetHoverDeferred() = false;
      });
    }
  });

  gw->GetContentNode()->OnMouseLeave([gw]() {
    NETSURF_LOCK;
    gw->GetHasPendingHover() = false;
    if (gw->GetBrowserWindow())
      browser_window_mouse_track(gw->GetBrowserWindow(), BROWSER_MOUSE_LEAVE, 0,
                                 0);
  });

  gw->GetContentNode()->OnMouseButtonDown(
      [gw](const Point& p, ::perception::window::MouseButton button) {
        NETSURF_LOCK;
        FlushPendingMouseHover(gw);
        if (button == ::perception::window::MouseButton::Right)
          return;

        int doc_x = static_cast<int>(p.x) + static_cast<int>(gw->GetScroll().x);
        int doc_y = static_cast<int>(p.y) + static_cast<int>(gw->GetScroll().y);

        gw->SetMouseDown(true);
        gw->SetDragStarted(false);
        gw->SetMouseButtonDown(button);
        gw->SetPressDocCoordinates(doc_x, doc_y);

        int mouse_state = gw->GetMouseModifiers();
        if (button == ::perception::window::MouseButton::Left)
          mouse_state |= BROWSER_MOUSE_PRESS_1;
        else if (button == ::perception::window::MouseButton::Middle)
          mouse_state |= BROWSER_MOUSE_PRESS_2;

        browser_window_mouse_click(
            gw->GetBrowserWindow(),
            static_cast<browser_mouse_state>(mouse_state), doc_x, doc_y);
      });

  gw->GetContentNode()->OnMouseButtonUp(
      [gw](const Point& p, ::perception::window::MouseButton button) {
        NETSURF_LOCK;
        FlushPendingMouseHover(gw);
        int doc_x = static_cast<int>(p.x) + static_cast<int>(gw->GetScroll().x);
        int doc_y = static_cast<int>(p.y) + static_cast<int>(gw->GetScroll().y);

        if (button == ::perception::window::MouseButton::Right) {
          gw->SetMouseDown(false);
          gw->SetDragStarted(false);
          ShowWebContextMenu(gw, p, doc_x, doc_y);
          return;
        }

        if (gw->HasDragStarted()) {
          gw->SetMouseDown(false);
          gw->SetDragStarted(false);
          browser_window_mouse_track(gw->GetBrowserWindow(),
                                     gw->GetMouseModifiers(), doc_x, doc_y);
        } else {
          gw->SetMouseDown(false);
          int mouse_state = gw->GetMouseModifiers();
          if (button == ::perception::window::MouseButton::Left) {
            mouse_state |= BROWSER_MOUSE_CLICK_1;
            mouse_state |= gw->RecordClickAndGetMultiClickFlags(doc_x, doc_y);
          } else if (button == ::perception::window::MouseButton::Middle) {
            mouse_state |= BROWSER_MOUSE_CLICK_2;
          }
          browser_window_mouse_click(
              gw->GetBrowserWindow(),
              static_cast<browser_mouse_state>(mouse_state), doc_x, doc_y);
        }
      });

  AddTab(gw);

  if (!GetGlobalUiWindow()) {
    // Construct browser chrome for the first time.
    auto go_back = []() {
      NETSURF_LOCK;
      if (GetActiveTab() && GetActiveTab()->GetBrowserWindow())
        browser_window_history_back(GetActiveTab()->GetBrowserWindow(), false);
    };

    auto go_forward = []() {
      NETSURF_LOCK;
      if (GetActiveTab() && GetActiveTab()->GetBrowserWindow()) {
        browser_window_history_forward(GetActiveTab()->GetBrowserWindow(),
                                       false);
      }
    };

    auto reload_or_stop = []() {
      NETSURF_LOCK;
      if (auto* active = GetActiveTab()) {
        if (active->IsThrobberRunning())
          browser_window_stop(active->GetBrowserWindow());
        else
          browser_window_reload(active->GetBrowserWindow(), false);
      }
    };

    auto vc = Container::VerticalContainer(
        [](ResizableContainerItem& item) {
          item.SetBehavior(ResizableContainerItem::Behavior::Flex);
        },
        [](Layout& layout) {
          layout.SetFlexGrow(1.0f);
          layout.SetAlignSelf(YGAlignStretch);
          layout.SetGap(0.0f);
          layout.SetPadding(YGEdgeAll, 0.0f);
        });
    SetViewportContainer(vc);
    vc->AddChild(gw->GetTabRootNode());

    auto docked_devtools_node = GetDockedDevToolsNode();
    g_viewport_devtools_split = ResizableContainer::VerticalContainer(
        [](Layout& layout) {
          layout.SetFlexGrow(1.0f);
          layout.SetFlexShrink(1.0f);
          layout.SetAlignSelf(YGAlignStretch);
          layout.SetPadding(YGEdgeAll, 0.0f);
        },
        vc, docked_devtools_node);

    SetDevToolsLayoutCallback([]() { SyncDockedDevToolsSplit(); });
    SyncDockedDevToolsSplit();

    std::shared_ptr<Node> back_btn_node = ImageButton::BasicImageButton(
        go_back, Image::LoadImage(kBackIconPath), &g_back_button,
        Tooltip::ShowTooltip("Back (Alt+Left) — Right-click for history"));
    back_btn_node->OnMouseButtonUp(
        [back_btn_node](const Point&,
                        ::perception::window::MouseButton button) {
          if (button == ::perception::window::MouseButton::Right) {
            NETSURF_LOCK;
            if (GetActiveTab() && GetActiveTab()->GetBrowserWindow()) {
              ShowLocalHistoryPopup(GetActiveTab()->GetBrowserWindow(),
                                    back_btn_node);
            }
          }
        });

    std::shared_ptr<Node> forward_btn_node = ImageButton::BasicImageButton(
        go_forward, Image::LoadImage(kForwardIconPath), &g_forward_button,
        Tooltip::ShowTooltip("Forward (Alt+Right) — Right-click for history"));
    forward_btn_node->OnMouseButtonUp(
        [forward_btn_node](const Point&,
                           ::perception::window::MouseButton button) {
          if (button == ::perception::window::MouseButton::Right) {
            NETSURF_LOCK;
            if (GetActiveTab() && GetActiveTab()->GetBrowserWindow()) {
              ShowLocalHistoryPopup(GetActiveTab()->GetBrowserWindow(),
                                    forward_btn_node);
            }
          }
        });

    g_reload_stop_button_node = ImageButton::BasicImageButton(
        reload_or_stop, Image::LoadImage(kRefreshIconPath),
        &g_reload_stop_image_button, Tooltip::ShowTooltip("Reload (Ctrl+R)"));

    g_security_button_node = ImageButton::BasicImageButtonWithSize(
        []() {
          NETSURF_LOCK;
          if (GetActiveTab() && GetActiveTab()->GetBrowserWindow() &&
              g_security_button_node) {
            ShowPageInfoPopup(GetActiveTab()->GetBrowserWindow(),
                              g_security_button_node);
          }
        },
        Image::LoadImage(kSearchIconPath), kOmniboxIconSize, kOmniboxIconSize,
        &g_security_image_button,
        [](Layout& l) {
          l.SetWidth(kOmniboxButtonSize);
          l.SetHeight(kOmniboxButtonSize);
          l.SetFlexShrink(0.0f);
        },
        Tooltip::ShowTooltip("View Site Information"));

    g_bookmark_star_button_node = ImageButton::BasicImageButtonWithSize(
        []() {
          NETSURF_LOCK;
          ToggleActivePageBookmark();
        },
        Image::LoadImage(kStarIconPath), kOmniboxIconSize, kOmniboxIconSize,
        &g_bookmark_star_image_button,
        [](Layout& l) {
          l.SetWidth(kOmniboxButtonSize);
          l.SetHeight(kOmniboxButtonSize);
          l.SetFlexShrink(0.0f);
        },
        Tooltip::ShowTooltip(
            "Bookmark This Page (Ctrl+D) — Right-click to edit"));
    g_bookmark_star_button_node->OnMouseButtonUp(
        [](const Point&, ::perception::window::MouseButton button) {
          if (button == ::perception::window::MouseButton::Right) {
            NETSURF_LOCK;
            EditActivePageBookmarkDialog();
          }
        });

    InputBox* url_input_ptr = nullptr;
    std::weak_ptr<Block> omnibox_block_weak;

    auto url_input_node = InputBox::BasicInputBox(
        "",
        [](Layout& layout) {
          layout.SetFlexGrow(1.0f);
          layout.SetFlexShrink(1.0f);
        },
        [](InputBox& input_box) {
          input_box.SetDrawBorder(false);
          input_box.OnTextChanged([](std::string_view text) {
            NETSURF_LOCK;
            UpdateUrlSuggestions(text);
          });
          input_box.OnEnterPressed([](std::string_view text) {
            NETSURF_LOCK;
            NavigateActiveTabToInput(text);
          });
        },
        &url_input_ptr);

    g_omnibox_node = Node::Empty(
        &omnibox_block_weak,
        [](Block& block) {
          block.SetFillColor(::perception::ui::kTextBoxBackgroundColor);
          block.SetBorderRadius(::perception::ui::kTextBoxCornerRadius);
          block.SetBorderWidth(::perception::ui::kTextBoxOutlineWidth);
          block.SetBorderColor(::perception::ui::kTextBoxOutlineColor);
        },
        [](Layout& layout) {
          layout.SetFlexGrow(1.0f);
          layout.SetFlexShrink(1.0f);
          layout.SetHeight(kOmniboxHeight);
          layout.SetFlexDirection(YGFlexDirectionRow);
          layout.SetAlignItems(YGAlignCenter);
          layout.SetPadding(YGEdgeHorizontal, kOmniboxHorizontalPadding);
        },
        g_security_button_node, url_input_node, g_bookmark_star_button_node);

    if (url_input_ptr) {
      url_input_ptr->OnFocusChanged([omnibox_block_weak](bool focused) {
        if (auto block = omnibox_block_weak.lock()) {
          block->SetBorderColor(
              focused ? ::perception::ui::kTextBoxOutlineFocusedColor
                      : ::perception::ui::kTextBoxOutlineColor);
          block->SetBorderWidth(
              focused ? ::perception::ui::kTextBoxOutlineFocusedWidth
                      : ::perception::ui::kTextBoxOutlineWidth);
        }
      });
    }

    g_overflow_button_node = ImageButton::BasicImageButton(
        []() {
          NETSURF_LOCK;
          ShowOverflowMenu(g_overflow_button_node);
        },
        Image::LoadImage(kMoreVerticalIconPath),
        Tooltip::ShowTooltip("Browser Menu"));

    g_loading_indicator_node = Block::SolidColor(
        kLoadingIndicatorColor, [](Layout& layout) {
          layout.SetAlignSelf(YGAlignStretch);
          layout.SetHeight(kLoadingIndicatorHeight);
          layout.SetDisplay(YGDisplayNone);
        });

    g_find_bar_node = Container::VerticalContainer(
        [](Layout& layout) {
          layout.SetAlignSelf(YGAlignStretch);
          layout.SetGap(0.0f);
          layout.SetPadding(YGEdgeAll, 0.0f);
          layout.SetDisplay(YGDisplayNone);
        },
        Container::HorizontalContainer(
            [](Layout& layout) {
              layout.SetAlignSelf(YGAlignStretch);
              layout.SetAlignItems(YGAlignCenter);
              layout.SetPadding(YGEdgeHorizontal, kStatusBarHorizontalPadding);
              layout.SetPadding(YGEdgeVertical, 4.0f);
            },
            ImageView::BasicImage(
                Image::LoadImage(kSearchIconPath),
                [](Layout& l) {
                  l.SetWidth(kFindBarIconSize);
                  l.SetHeight(kFindBarIconSize);
                },
                [](ImageView& iv) {
                  iv.SetResizeMethod(ResizeMethod::Contain);
                  iv.SetAlignment(TextAlignment::MiddleCenter);
                }),
            InputBox::BasicInputBox(
                "", [](Layout& l) { l.SetWidth(kFindInputWidth); },
                [](InputBox& box) {
                  box.OnTextChanged([](std::string_view) {
                    NETSURF_LOCK;
                    RunFindInPage(true);
                  });
                  box.OnEnterPressed([](std::string_view) {
                    NETSURF_LOCK;
                    RunFindInPage(true);
                  });
                },
                &g_find_input),
            ImageButton::BasicImageButton(
                []() {
                  NETSURF_LOCK;
                  RunFindInPage(false);
                },
                Image::LoadImage(kChevronUpIconPath), &g_find_prev_button,
                Tooltip::ShowTooltip("Previous Match")),
            ImageButton::BasicImageButton(
                []() {
                  NETSURF_LOCK;
                  RunFindInPage(true);
                },
                Image::LoadImage(kChevronDownIconPath), &g_find_next_button,
                Tooltip::ShowTooltip("Next Match (Enter)")),
            ImageButton::BasicImageButton(
                []() {
                  NETSURF_LOCK;
                  g_find_case_sensitive = !g_find_case_sensitive;
                  if (g_find_case_button)
                    g_find_case_button->SetToggled(g_find_case_sensitive);
                  RunFindInPage(true);
                },
                Image::LoadImage(kCaseSensitiveIconPath), &g_find_case_button,
                Tooltip::ShowTooltip("Match Case")),
            ImageButton::BasicImageButton(
                []() {
                  NETSURF_LOCK;
                  g_find_show_all = !g_find_show_all;
                  if (g_find_highlight_button)
                    g_find_highlight_button->SetToggled(g_find_show_all);
                  RunFindInPage(true);
                },
                Image::LoadImage(kHighlightIconPath), &g_find_highlight_button,
                Tooltip::ShowTooltip("Highlight All")),
            Label::BasicLabel("", &g_find_status_label),
            Node::Empty([](Layout& l) { l.SetFlexGrow(1.0f); }),
            ImageButton::BasicImageButton(
                []() {
                  NETSURF_LOCK;
                  CloseFindBar();
                },
                Image::LoadImage(kCloseIconPath),
                Tooltip::ShowTooltip("Close Find Bar (Esc)"))),
        Block::SolidColor(::perception::ui::kContainerBorderColor,
                          [](Layout& layout) {
                            layout.SetAlignSelf(YGAlignStretch);
                            layout.SetHeight(1.0f);
                          }));

    Label* status_label_ptr = nullptr;

    g_download_badge_node = Button::BasicButton(
        []() {
          NETSURF_LOCK;
          ShowDownloadsWindow();
        },
        [](Button& btn) { btn.SetButtonStyle(Button::ButtonStyle::GHOST); },
        [](Layout& l) {
          l.SetHeight(kStatusBarBadgeHeight);
          l.SetMinHeight(kStatusBarBadgeHeight);
          l.SetMinWidth(0.0f);
          l.SetFlexDirection(YGFlexDirectionRow);
          l.SetAlignItems(YGAlignCenter);
          l.SetPadding(YGEdgeHorizontal, kStatusBarBadgePadding);
          l.SetDisplay(YGDisplayNone);
        },
        Tooltip::ShowTooltip("Downloads (Ctrl+J)"),
        ImageView::BasicImage(
            Image::LoadImage(kDownloadIconPath),
            [](Layout& l) {
              l.SetWidth(kStatusBarBadgeIconSize);
              l.SetHeight(kStatusBarBadgeIconSize);
              l.SetMargin(YGEdgeRight, 4.0f);
            },
            [](ImageView& iv) {
              iv.SetResizeMethod(ResizeMethod::Contain);
              iv.SetAlignment(TextAlignment::MiddleCenter);
            }),
        Label::BasicLabel("", &g_download_badge_label));

    auto console_badge_node = Button::BasicButton(
        []() {
          NETSURF_LOCK;
          ToggleDevTools(DevToolsPanel::Console);
        },
        [](Button& btn) { btn.SetButtonStyle(Button::ButtonStyle::GHOST); },
        [](Layout& l) {
          l.SetHeight(kStatusBarBadgeHeight);
          l.SetMinHeight(kStatusBarBadgeHeight);
          l.SetMinWidth(0.0f);
          l.SetFlexDirection(YGFlexDirectionRow);
          l.SetAlignItems(YGAlignCenter);
          l.SetPadding(YGEdgeHorizontal, kStatusBarBadgePadding);
        },
        Tooltip::ShowTooltip("Developer Tools Console (F12)"),
        ImageView::BasicImage(
            Image::LoadImage(kTerminalIconPath), &g_console_badge_image_view,
            [](Layout& l) {
              l.SetWidth(kStatusBarBadgeIconSize);
              l.SetHeight(kStatusBarBadgeIconSize);
              l.SetMargin(YGEdgeRight, 4.0f);
            },
            [](ImageView& iv) {
              iv.SetResizeMethod(ResizeMethod::Contain);
              iv.SetAlignment(TextAlignment::MiddleCenter);
            }),
        Label::BasicLabel("DevTools", &g_console_badge_label, [](Label& lbl) {
          lbl.SetColor(::perception::ui::kSecondaryTextColor);
        }));

    auto status_bar = Container::HorizontalContainer(
        [](Layout& layout) {
          layout.SetAlignSelf(YGAlignStretch);
          layout.SetHeight(kStatusBarHeight);
          layout.SetAlignItems(YGAlignCenter);
          layout.SetPadding(YGEdgeHorizontal, kStatusBarHorizontalPadding);
        },
        Label::SingleLineTruncated(
            "",
            [](Layout& layout) {
              layout.SetFlexGrow(1.0f);
              layout.SetFlexShrink(1.0f);
            },
            [](Label& lbl) {
              lbl.SetTextAlignment(TextAlignment::MiddleLeft);
            },
            &status_label_ptr),
        g_download_badge_node, console_badge_node);

    std::shared_ptr<TabBar> tab_bar_ptr;

    auto win = UiWindow::ResizableWindowWithTabBar(
        &tab_bar_ptr,
        [](UiWindow& win) {
          win.OnClose([]() { gui_quit(); });
          win.OnResize([]() {
            NETSURF_LOCK;
            for (auto gw : GetOpenTabs()) {
              if (gw && gw->GetBrowserWindow()) {
                browser_window_schedule_reformat(gw->GetBrowserWindow());
                if (gw->GetContentNode())
                  gw->GetContentNode()->Invalidate();
              }
            }
          });
        },
        [](Layout& layout) {
          layout.SetPadding(YGEdgeHorizontal, 0.0f);
          layout.SetPadding(YGEdgeBottom, 0.0f);
        },
        Container::VerticalContainer(
            [](Layout& layout) {
              layout.SetFlexGrow(1.0f);
              layout.SetAlignSelf(YGAlignStretch);
              layout.SetGap(0.0f);
            },
            Container::HorizontalContainer(
                [](Layout& layout) {
                  layout.SetAlignSelf(YGAlignStretch);
                  layout.SetAlignItems(YGAlignCenter);
                  layout.SetPadding(YGEdgeHorizontal,
                                    ::perception::ui::kUiWindowPadding);
                  layout.SetPadding(YGEdgeBottom, 4.0f);
                },
                back_btn_node, forward_btn_node, g_reload_stop_button_node,
                g_omnibox_node, g_overflow_button_node),
            Block::SolidColor(::perception::ui::kContainerBorderColor,
                              [](Layout& layout) {
                                layout.SetAlignSelf(YGAlignStretch);
                                layout.SetHeight(1.0f);
                              }),
            g_loading_indicator_node, g_find_bar_node,
            g_viewport_devtools_split,
            Block::SolidColor(::perception::ui::kContainerBorderColor,
                              [](Layout& layout) {
                                layout.SetAlignSelf(YGAlignStretch);
                                layout.SetHeight(1.0f);
                              }),
            status_bar));

    SetGlobalUiWindow(win);
    SetGlobalTabBar(tab_bar_ptr);
    SetGlobalUrlInput(url_input_ptr);
    SetGlobalStatusLabel(status_label_ptr);

    AddDownloadListener([]() { UpdateStatusBarBadges(); });
    AddConsoleListener([]() { UpdateStatusBarBadges(); });

    if (!win->GetChildren().empty()) {
      win->GetChildren().front()->Apply(
          [](Layout& layout) { layout.SetMargin(YGEdgeHorizontal, 0.0f); });
    }

    tab_bar_ptr->SetPrefixNode(Label::BasicLabel(
        "NetSurf", [](Label& lbl) { lbl.SetColor(kWhiteIconTint); },
        [](Layout& layout) {
          layout.SetMargin(YGEdgeLeft,
                           ::perception::ui::kUiWindowPadding * 2.0f);
          layout.SetMargin(YGEdgeRight, ::perception::ui::kUiWindowPadding);
        }));

    tab_bar_ptr->SetSuffixNode(ImageButton::BasicImageButton(
        []() {
          ::perception::DeferAfterEvents([]() {
            NETSURF_LOCK;
            OpenNewTab();
          });
        },
        Image::LoadImage(kPlusIconPath),
        [](ImageButton& btn) { btn.SetColor(kWhiteIconTint); },
        [](Layout& layout) {
          layout.SetMargin(YGEdgeRight, ::perception::ui::kUiWindowPadding);
        },
        Tooltip::ShowTooltip("New Tab (Ctrl+T)")));

    tab_bar_ptr->OnTabSelected([](int index) { SwitchTab(index); });
    tab_bar_ptr->OnTabClosed([](int index) { CloseTab(index); });

    SetActiveTabIndex(0);
    UpdateBrowserToolbar();
    UpdateStatusBarBadges();
  } else {
    bool foreground =
        (flags & GW_CREATE_FOREGROUND) != 0 || nsoption_bool(foreground_new);
    if (foreground) {
      SwitchTab(GetTabCount() - 1);
      if ((flags & GW_CREATE_FOCUS_LOCATION) != 0 && GetGlobalUrlInput()) {
        GetGlobalUrlInput()->Focus();
        GetGlobalUrlInput()->SelectAll();
      }
    }
  }

  UpdateTabBar();
  if (GetGlobalUiWindow())
    GetGlobalUiWindow()->Invalidate();

  return gw;
}

static void gui_window_destroy(struct gui_window* gw) {
  NETSURF_LOCK;
  HandleTabDestroyed(gw);
  delete gw;
}

}  // namespace

struct gui_window_table perception_window_table = {
    .create = gui_window_create,
    .destroy = gui_window_destroy,
    .invalidate = FbWindowInvalidateArea,
    .get_scroll = GuiWindowGetScroll,
    .set_scroll = GuiWindowSetScroll,
    .get_dimensions = GuiWindowGetDimensions,
    .event = GuiWindowEvent,

    .set_title = GuiWindowSetTitle,
    .set_url = GuiWindowSetUrl,
    .set_icon = GuiWindowSetIcon,
    .set_status = GuiWindowSetStatus,
    .set_pointer = GuiWindowSetPointer,
    .place_caret = GuiWindowPlaceCaret,
    .save_link = GuiWindowSaveLink,
    .file_gadget_open = GuiWindowFileGadgetOpen,
    .console_log = GuiWindowConsoleLog,
};

struct gui_search_table perception_search_table = {
    .status = SearchStatus,
    .hourglass = SearchHourglass,
    .add_recent = SearchAddRecent,
    .forward_state = SearchForwardState,
    .back_state = SearchBackState,
};

}  // namespace perception
}  // namespace netsurf
