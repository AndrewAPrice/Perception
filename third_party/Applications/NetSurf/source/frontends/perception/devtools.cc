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

#include "devtools.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "bitmap.h"
#include "include/core/SkImage.h"
#include "managers.h"
#include "network_log.h"
#include "perception/clipboard.h"
#include "perception/time.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/combo_box.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/file_dialog.h"
#include "perception/ui/components/image_button.h"
#include "perception/ui/components/image_view.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/resizable_container.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/table.h"
#include "perception/ui/components/tooltip.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/font.h"
#include "perception/ui/image.h"
#include "perception/ui/layout.h"
#include "perception/ui/resize_method.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/theme.h"
#include "resources.h"
#include "source_viewer.h"
#include "tabs.h"
#include "window.h"

extern "C" {
#include <libwapcaplet/libwapcaplet.h>

#include "content/content.h"
#include "netsurf/browser_window.h"
#include "netsurf/content.h"
#include "netsurf/content_type.h"
#include "utils/errors.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"
}

namespace {

// Maximum number of console log entries retained per browser tab.
constexpr size_t kMaxConsoleMessagesPerTab = 2000;

// Default height in pixels of the docked Developer Tools pane.
constexpr float kDefaultDockedHeight = 200.0f;

// Minimum height in pixels of the docked Developer Tools pane.
constexpr float kMinDockedHeight = 120.0f;

// Width in pixels of the filter input boxes in action bars.
constexpr float kFilterInputWidth = 180.0f;

// Width in pixels of the key column in the Page Info panel.
constexpr float kPageInfoKeyColumnWidth = 180.0f;

// Width in pixels of the Type column in the Resources table.
constexpr float kResColTypeWidth = 90.0f;

// Width in pixels of the Name column in the Resources table.
constexpr float kResColNameWidth = 150.0f;

// Width in pixels of the MIME Type column in the Resources table.
constexpr float kResColMimeWidth = 130.0f;

// Width in pixels of the Size column in the Resources table.
constexpr float kResColSizeWidth = 85.0f;

// Width in pixels of the Status column in the Resources table.
constexpr float kResColStatusWidth = 95.0f;

// Width in pixels of the Dimensions column in the Resources table.
constexpr float kResColDimsWidth = 95.0f;

// Width in pixels of the index column in the Network table.
constexpr float kNetColIndexWidth = 45.0f;

// Width in pixels of the Method column in the Network table.
constexpr float kNetColMethodWidth = 65.0f;

// Width in pixels of the Status column in the Network table.
constexpr float kNetColStatusWidth = 90.0f;

// Width in pixels of the Name column in the Network table.
constexpr float kNetColNameWidth = 140.0f;

// Width in pixels of the Type column in the Network table.
constexpr float kNetColTypeWidth = 110.0f;

// Width in pixels of the Size column in the Network table.
constexpr float kNetColSizeWidth = 80.0f;

// Width in pixels of the Time column in the Network table.
constexpr float kNetColTimeWidth = 75.0f;

// Width in pixels of the Waterfall column in the Network table.
constexpr float kNetColWaterfallWidth = 120.0f;

// Number of character slots inside the ASCII network waterfall bar.
constexpr int kWaterfallBarSlots = 12;

// Height of the image preview container in the Resources panel.
constexpr float kImagePreviewMaxHeight = 180.0f;

// Size of the level icon inside Console message rows.
constexpr float kConsoleIconSize = 14.0f;

// Width of the timestamp label in Console message rows.
constexpr float kConsoleTimestampWidth = 76.0f;

// Width of the source badge label in Console message rows.
constexpr float kConsoleSourceWidth = 56.0f;

// Background fill color for console error rows.
constexpr uint32_t kConsoleErrorRowBgColor = 0xFFFEF2F2;

// Background fill color for console warning rows.
constexpr uint32_t kConsoleWarnRowBgColor = 0xFFFFFBEB;

// Text color for console error messages.
constexpr uint32_t kConsoleErrorTextColor = 0xFFB91C1C;

// Text color for console warning messages.
constexpr uint32_t kConsoleWarnTextColor = 0xFFB45309;

// Text color for console JS input echo rows.
constexpr uint32_t kConsoleInputTextColor = 0xFF4F46E5;

// Text color for console debug messages.
constexpr uint32_t kConsoleDebugTextColor = 0xFF6B7280;

// Icon tint color for console error icons.
constexpr uint32_t kConsoleErrorIconTint = 0xFFDC2626;

// Icon tint color for console warning icons.
constexpr uint32_t kConsoleWarnIconTint = 0xFFD97706;

// Path to the clear-all icon asset.
constexpr std::string_view kClearAllIconPath =
    "/Applications/NetSurf/clear-all.svg";

// Path to the pin (preserve log) icon asset.
constexpr std::string_view kPinIconPath = "/Applications/NetSurf/pin.svg";

// Path to the filter icon asset.
constexpr std::string_view kFilterIconPath = "/Applications/NetSurf/filter.svg";

// Path to the refresh icon asset.
constexpr std::string_view kRefreshIconPath =
    "/Applications/NetSurf/refresh.svg";

// Path to the copy icon asset.
constexpr std::string_view kCopyIconPath = "/Applications/NetSurf/copy.svg";

// Path to the save icon asset.
constexpr std::string_view kSaveIconPath = "/Applications/NetSurf/save.svg";

// Path to the search icon asset.
constexpr std::string_view kSearchIconPath = "/Applications/NetSurf/search.svg";

// Path to the external-link (pop-out / open in new tab) icon asset.
constexpr std::string_view kExternalLinkIconPath =
    "/Applications/NetSurf/external-link.svg";

// Path to the dock-bottom icon asset.
constexpr std::string_view kDockBottomIconPath =
    "/Applications/NetSurf/dock-bottom.svg";

// Path to the close icon asset.
constexpr std::string_view kCloseIconPath = "/Applications/NetSurf/close.svg";

// Path to the cookie icon asset.
constexpr std::string_view kCookieIconPath = "/Applications/NetSurf/cookie.svg";

// Path to the error alert circle icon asset.
constexpr std::string_view kAlertCircleIconPath =
    "/Applications/NetSurf/alert-circle.svg";

// Path to the warning alert triangle icon asset.
constexpr std::string_view kAlertTriangleIconPath =
    "/Applications/NetSurf/alert-triangle.svg";

}  // namespace

namespace netsurf {
namespace perception {

namespace {

using ::perception::ui::GetBold12UiFont;
using ::perception::ui::GetMonospace12UiFont;
using ::perception::ui::Image;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::ResizeMethod;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::ComboBox;
using ::perception::ui::components::Container;
using ::perception::ui::components::ImageButton;
using ::perception::ui::components::ImageView;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::ResizableContainer;
using ::perception::ui::components::ResizableContainerItem;
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::ShowSaveFileDialog;
using ::perception::ui::components::Table;
using ::perception::ui::components::Tooltip;
using ::perception::ui::components::UiWindow;

// Returns the current monotonic timestamp in milliseconds.
int64_t CurrentTimeMs() {
  return static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ::perception::GetTimeSinceKernelStarted())
          .count());
}

// Case-insensitive substring search helper.
bool ContainsIgnoreCase(std::string_view haystack, std::string_view needle) {
  if (needle.empty())
    return true;
  if (haystack.size() < needle.size())
    return false;
  auto it = std::search(
      haystack.begin(), haystack.end(), needle.begin(), needle.end(),
      [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
      });
  return it != haystack.end();
}

// Formats a byte count into a human-readable B / KB / MB string.
std::string FormatByteSize(size_t bytes) {
  char buf[64];
  if (bytes < 1024) {
    snprintf(buf, sizeof(buf), "%zu B", bytes);
  } else if (bytes < 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / 1024.0);
  } else {
    snprintf(buf, sizeof(buf), "%.2f MB",
             static_cast<double>(bytes) / (1024.0 * 1024.0));
  }
  return buf;
}

// Extracts the leaf filename from a URL for compact display.
std::string ExtractUrlLeafName(std::string_view url) {
  if (url.empty())
    return "(empty)";
  std::string_view clean = url;
  size_t hash = clean.find('#');
  if (hash != std::string_view::npos)
    clean = clean.substr(0, hash);
  size_t query = clean.find('?');
  if (query != std::string_view::npos)
    clean = clean.substr(0, query);
  if (clean.size() > 1 && clean.back() == '/')
    clean.remove_suffix(1);
  size_t slash = clean.rfind('/');
  if (slash != std::string_view::npos && slash + 1 < clean.size())
    return std::string(clean.substr(slash + 1));
  return std::string(url);
}

// Adapter helpers for NetworkLogEntry fields across header revisions.
namespace net_compat {

const std::deque<NetworkLogEntry>& FetchEntries() {
  return ::netsurf::perception::GetNetworkLog();
}

template <typename Entry>
int GetHttpStatus(const Entry& e) {
  if constexpr (requires { e.http_status; })
    return static_cast<int>(e.http_status);
  else if constexpr (requires { e.http_code; })
    return static_cast<int>(e.http_code);
  else
    return 0;
}

template <typename Entry>
std::string GetMimeType(const Entry& e) {
  if constexpr (requires { e.mime_type; })
    return e.mime_type;
  else if constexpr (requires { e.content_type; })
    return e.content_type;
  else
    return "";
}

template <typename Entry>
std::string GetRedirectUrl(const Entry& e) {
  if constexpr (requires { e.redirect_url; })
    return e.redirect_url;
  else if constexpr (requires { e.redirect_target; })
    return e.redirect_target;
  else
    return "";
}

template <typename Entry>
int64_t GetFirstByteTimeMs(const Entry& e) {
  if constexpr (requires { e.first_byte_time_ms; })
    return static_cast<int64_t>(e.first_byte_time_ms);
  else if constexpr (requires { e.ttfb_ms; })
    return static_cast<int64_t>(e.ttfb_ms);
  else
    return 0;
}

template <typename Entry>
int64_t GetDurationMs(const Entry& e) {
  if constexpr (requires { e.duration_ms; })
    if (e.duration_ms > 0)
      return static_cast<int64_t>(e.duration_ms);
  if (e.end_time_ms > e.start_time_ms && e.start_time_ms > 0)
    return static_cast<int64_t>(e.end_time_ms - e.start_time_ms);
  return 0;
}

template <typename Entry>
std::string GetStatusString(const Entry& e) {
  int code = GetHttpStatus(e);
  if constexpr (requires { std::string_view(e.status); }) {
    if (code > 0 && !e.status.empty())
      return std::to_string(code) + " (" + e.status + ")";
    if (code > 0)
      return std::to_string(code);
    return e.status.empty() ? "Pending" : e.status;
  } else if constexpr (requires { e.error_message; }) {
    if (!e.error_message.empty())
      return code > 0 ? std::to_string(code) + " Error" : "Error";
    if (code > 0)
      return std::to_string(code);
    if (e.end_time_ms > 0)
      return "OK";
    return "Pending";
  } else {
    return code > 0 ? std::to_string(code) : "Pending";
  }
}

template <typename Entry>
std::string GetErrorMessage(const Entry& e) {
  if constexpr (requires { e.error_message; })
    return e.error_message;
  else if constexpr (requires { std::string_view(e.status); })
    return e.status;
  else
    return "";
}

template <typename Entry>
std::string FormatRequestHeaders(const Entry& e) {
  if constexpr (requires { std::string_view(e.request_headers); }) {
    return std::string(e.request_headers);
  } else if constexpr (requires { e.request_headers.begin(); }) {
    std::string out;
    for (const auto& line : e.request_headers) {
      out += line;
      out += "\n";
    }
    return out;
  } else {
    return "";
  }
}

}  // namespace net_compat

// Interactive horizontal segmented button bar for switching panels or filters.
class SegmentedOptionBar {
 public:
  static std::shared_ptr<SegmentedOptionBar> Create(
      const std::vector<std::string>& labels,
      int initial_index,
      std::function<void(int)> on_select) {
    auto bar = std::shared_ptr<SegmentedOptionBar>(
        new SegmentedOptionBar(initial_index, std::move(on_select)));
    bar->Build(labels);
    return bar;
  }

  std::shared_ptr<Node> GetNode() const { return container_node_; }

  int GetSelectedIndex() const { return selected_index_; }

  void SetSelectedIndex(int index, bool notify = false) {
    if (index < 0 || index >= static_cast<int>(buttons_.size()))
      return;
    selected_index_ = index;
    UpdateButtonStyles();
    if (notify && on_select_)
      on_select_(selected_index_);
  }

 private:
  SegmentedOptionBar(int initial_index, std::function<void(int)> on_select)
      : selected_index_(initial_index), on_select_(std::move(on_select)) {}

  void Build(const std::vector<std::string>& labels) {
    container_node_ = Container::HorizontalContainer(
        [](Block& block) {
          block.SetFillColor(::perception::ui::kScrollContainerBackgroundColor);
          block.SetBorderColor(::perception::ui::kButtonBorderColor);
          block.SetBorderWidth(1.0f);
          block.SetBorderRadius(::perception::ui::kButtonBorderRadius);
        },
        [](Layout& layout) {
          layout.SetAlignItems(YGAlignCenter);
          layout.SetPadding(YGEdgeAll, 2.0f);
          layout.SetGap(2.0f);
          layout.SetFlexShrink(0.0f);
        });

    std::weak_ptr<SegmentedOptionBar> weak_self;
    buttons_.resize(labels.size());
    for (size_t i = 0; i < labels.size(); ++i) {
      int idx = static_cast<int>(i);
      SegmentedOptionBar* raw_self = this;
      auto btn_node = Button::TextButton(
          labels[i], [raw_self, idx]() { raw_self->SetSelectedIndex(idx, true); },
          &buttons_[i]);
      container_node_->AddChild(btn_node);
    }
    UpdateButtonStyles();
  }

  void UpdateButtonStyles() {
    for (size_t i = 0; i < buttons_.size(); ++i) {
      if (!buttons_[i])
        continue;
      bool active = (static_cast<int>(i) == selected_index_);
      buttons_[i]->SetButtonStyle(active ? Button::ButtonStyle::PRIMARY
                                         : Button::ButtonStyle::GHOST);
      buttons_[i]->SetToggled(active);
    }
    if (container_node_)
      container_node_->Invalidate();
  }

  std::shared_ptr<Node> container_node_;
  std::vector<std::shared_ptr<Button>> buttons_;
  int selected_index_ = 0;
  std::function<void(int)> on_select_;
};

// Table data source for the Resources panel.
class ResourcesTableDataSource : public Table::DataSource {
 public:
  void SetResources(std::vector<PageResource> resources) {
    resources_ = std::move(resources);
    if (sort_col_ >= 0)
      SortByColumn(sort_col_, sort_asc_);
  }

  const std::vector<PageResource>& GetResources() const { return resources_; }

  int GetNumberOfRows() override {
    return static_cast<int>(resources_.size());
  }

  std::string GetCellValue(int row_index, int column_index) override {
    if (row_index < 0 || row_index >= static_cast<int>(resources_.size()))
      return "";
    const auto& r = resources_[row_index];
    switch (column_index) {
      case 0:
        return std::string(ResourceCategoryToString(r.category));
      case 1:
        return r.name.empty() ? ExtractUrlLeafName(r.url) : r.name;
      case 2:
        return r.url;
      case 3:
        return r.mime_type.empty() ? "-" : r.mime_type;
      case 4:
        return r.size_bytes > 0 ? FormatByteSize(r.size_bytes) : "-";
      case 5:
        return r.is_internal ? "Internal"
                             : (r.status.empty() ? "Ready" : r.status);
      case 6:
        if (r.width > 0 && r.height > 0)
          return std::to_string(r.width) + "x" + std::to_string(r.height);
        return "-";
      default:
        return "";
    }
  }

  void SortByColumn(int column_index, bool ascending) override {
    sort_col_ = column_index;
    sort_asc_ = ascending;
    std::stable_sort(
        resources_.begin(), resources_.end(),
        [column_index, ascending](const PageResource& a,
                                  const PageResource& b) {
          int cmp = 0;
          switch (column_index) {
            case 0:
              cmp = std::string_view(ResourceCategoryToString(a.category))
                        .compare(ResourceCategoryToString(b.category));
              break;
            case 1:
              cmp = a.name.compare(b.name);
              break;
            case 2:
              cmp = a.url.compare(b.url);
              break;
            case 3:
              cmp = a.mime_type.compare(b.mime_type);
              break;
            case 4:
              cmp = (a.size_bytes < b.size_bytes)
                        ? -1
                        : (a.size_bytes > b.size_bytes ? 1 : 0);
              break;
            case 5:
              cmp = a.status.compare(b.status);
              break;
            case 6: {
              int area_a = a.width * a.height;
              int area_b = b.width * b.height;
              cmp = (area_a < area_b) ? -1 : (area_a > area_b ? 1 : 0);
              break;
            }
            default:
              break;
          }
          return ascending ? (cmp < 0) : (cmp > 0);
        });
  }

  ::perception::ui::components::CellHighlightability GetCellHighlightablity(
      int, int) override {
    return ::perception::ui::components::CellHighlightability::RowHighlightable;
  }

 private:
  std::vector<PageResource> resources_;
  int sort_col_ = -1;
  bool sort_asc_ = true;
};

// Table data source for the Network panel.
class NetworkTableDataSource : public Table::DataSource {
 public:
  void SetEntries(std::vector<NetworkLogEntry> entries) {
    entries_ = std::move(entries);
    min_start_ms_ = 0;
    max_end_ms_ = 0;
    for (const auto& e : entries_) {
      int64_t start = static_cast<int64_t>(e.start_time_ms);
      int64_t end = static_cast<int64_t>(e.end_time_ms);
      if (end < start)
        end = start;
      if (start > 0 && (min_start_ms_ == 0 || start < min_start_ms_))
        min_start_ms_ = start;
      if (end > max_end_ms_)
        max_end_ms_ = end;
    }
    if (sort_col_ >= 0)
      SortByColumn(sort_col_, sort_asc_);
  }

  const std::vector<NetworkLogEntry>& GetEntries() const { return entries_; }

  int GetNumberOfRows() override { return static_cast<int>(entries_.size()); }

  std::string GetCellValue(int row_index, int column_index) override {
    if (row_index < 0 || row_index >= static_cast<int>(entries_.size()))
      return "";
    const auto& e = entries_[row_index];
    switch (column_index) {
      case 0:
        return std::to_string(e.id);
      case 1:
        return e.method.empty() ? "GET" : e.method;
      case 2:
        return net_compat::GetStatusString(e);
      case 3:
        return ExtractUrlLeafName(e.url);
      case 4:
        return e.url;
      case 5: {
        std::string mime = net_compat::GetMimeType(e);
        return mime.empty() ? (e.scheme.empty() ? "-" : e.scheme) : mime;
      }
      case 6:
        return e.bytes_received > 0 ? FormatByteSize(e.bytes_received) : "-";
      case 7: {
        int64_t dur = net_compat::GetDurationMs(e);
        return dur > 0 ? std::to_string(dur) + " ms" : "-";
      }
      case 8:
        return FormatWaterfall(e);
      default:
        return "";
    }
  }

  void SortByColumn(int column_index, bool ascending) override {
    sort_col_ = column_index;
    sort_asc_ = ascending;
    std::stable_sort(
        entries_.begin(), entries_.end(),
        [column_index, ascending](const NetworkLogEntry& a,
                                  const NetworkLogEntry& b) {
          int cmp = 0;
          switch (column_index) {
            case 0:
              cmp = (a.id < b.id) ? -1 : (a.id > b.id ? 1 : 0);
              break;
            case 1:
              cmp = a.method.compare(b.method);
              break;
            case 2: {
              int sa = net_compat::GetHttpStatus(a);
              int sb = net_compat::GetHttpStatus(b);
              cmp = (sa < sb) ? -1 : (sa > sb ? 1 : 0);
              break;
            }
            case 3:
              cmp = ExtractUrlLeafName(a.url).compare(
                  ExtractUrlLeafName(b.url));
              break;
            case 4:
              cmp = a.url.compare(b.url);
              break;
            case 5:
              cmp = net_compat::GetMimeType(a).compare(
                  net_compat::GetMimeType(b));
              break;
            case 6:
              cmp = (a.bytes_received < b.bytes_received)
                        ? -1
                        : (a.bytes_received > b.bytes_received ? 1 : 0);
              break;
            case 7: {
              int64_t da = net_compat::GetDurationMs(a);
              int64_t db = net_compat::GetDurationMs(b);
              cmp = (da < db) ? -1 : (da > db ? 1 : 0);
              break;
            }
            case 8:
              cmp = (a.start_time_ms < b.start_time_ms)
                        ? -1
                        : (a.start_time_ms > b.start_time_ms ? 1 : 0);
              break;
            default:
              break;
          }
          return ascending ? (cmp < 0) : (cmp > 0);
        });
  }

  ::perception::ui::components::CellHighlightability GetCellHighlightablity(
      int, int) override {
    return ::perception::ui::components::CellHighlightability::RowHighlightable;
  }

 private:
  std::string FormatWaterfall(const NetworkLogEntry& e) const {
    int64_t total_span = max_end_ms_ - min_start_ms_;
    if (total_span <= 0 || min_start_ms_ <= 0)
      return "[============]";
    int64_t start = static_cast<int64_t>(e.start_time_ms) - min_start_ms_;
    if (start < 0)
      start = 0;
    int64_t dur = net_compat::GetDurationMs(e);
    if (dur < 0)
      dur = 0;

    int start_slot =
        static_cast<int>((start * kWaterfallBarSlots) / total_span);
    int end_slot =
        static_cast<int>(((start + dur) * kWaterfallBarSlots) / total_span);
    start_slot = std::clamp(start_slot, 0, kWaterfallBarSlots - 1);
    end_slot = std::clamp(end_slot, start_slot + 1, kWaterfallBarSlots);

    std::string bar = "[";
    for (int i = 0; i < kWaterfallBarSlots; ++i) {
      if (i >= start_slot && i < end_slot)
        bar.push_back('=');
      else
        bar.push_back(' ');
    }
    bar.push_back(']');
    return bar;
  }

  std::vector<NetworkLogEntry> entries_;
  int64_t min_start_ms_ = 0;
  int64_t max_end_ms_ = 0;
  int sort_col_ = -1;
  bool sort_asc_ = true;
};

// Reads the source text for a PageResource from its inline source or cache handle.
std::string ReadResourceText(const PageResource& res) {
  if (!res.inline_source.empty())
    return res.inline_source;
  if (res.handle != nullptr) {
    NETSURF_LOCK;
    size_t size = 0;
    const auto* data = content_get_source_data(res.handle, &size);
    if (data != nullptr && size > 0)
      return std::string(reinterpret_cast<const char*>(data), size);
  }
  return "";
}

// Returns true if a PageResource can be displayed as text/code.
bool IsTextResource(const PageResource& res) {
  if (res.category == ResourceCategory::kDocument ||
      res.category == ResourceCategory::kStylesheet ||
      res.category == ResourceCategory::kScript ||
      res.category == ResourceCategory::kFrame)
    return true;
  if (ContainsIgnoreCase(res.mime_type, "text/") ||
      ContainsIgnoreCase(res.mime_type, "json") ||
      ContainsIgnoreCase(res.mime_type, "xml") ||
      ContainsIgnoreCase(res.mime_type, "javascript") ||
      ContainsIgnoreCase(res.mime_type, "svg"))
    return true;
  return false;
}

// Global Console and Developer Tools state.
struct DevToolsState {
  std::unordered_map<Window*, std::deque<ConsoleMessage>> tab_logs;
  std::unordered_map<Window*, int64_t> tab_nav_start_ms;
  std::unordered_map<uint64_t, std::function<void()>> console_listeners;
  uint64_t next_console_listener_id = 1;

  bool is_initialized = false;
  bool is_open = false;
  bool is_docked = true;
  bool is_closing_popup_internally = false;
  DevToolsPanel active_panel = DevToolsPanel::Console;
  std::function<void()> layout_callback;

  // Root nodes.
  std::shared_ptr<Node> docked_host_node;
  std::shared_ptr<Node> root_node;
  std::shared_ptr<Node> popup_window_node;
  std::shared_ptr<SegmentedOptionBar> main_tab_bar;
  std::shared_ptr<ImageButton> dock_toggle_image_button;
  std::shared_ptr<Node> dock_toggle_node;
  std::shared_ptr<Node> close_button_node;
  std::shared_ptr<Image> popout_icon;
  std::shared_ptr<Image> dock_icon;

  // Panel container nodes (one per DevToolsPanel).
  std::shared_ptr<Node> panel_nodes[6];

  // Console panel widgets and state.
  bool preserve_console_log = false;
  int console_level_filter = 0;
  std::string console_text_filter;
  std::shared_ptr<Button> console_preserve_btn;
  std::shared_ptr<Node> console_messages_container;
  std::shared_ptr<ScrollContainer> console_scroll_container;
  std::shared_ptr<InputBox> console_js_input;

  // Source panel widgets and state.
  std::vector<PageResource> source_resources;
  int selected_source_index = 0;
  std::shared_ptr<ComboBox> source_combo_box;
  std::shared_ptr<SourceViewer> source_viewer;

  // Resources panel widgets and state.
  std::vector<PageResource> all_page_resources;
  std::string resources_filter;
  int selected_resource_index = -1;
  std::shared_ptr<Label> resources_summary_label;
  std::shared_ptr<ResourcesTableDataSource> resources_data_source;
  std::shared_ptr<Table> resources_table;
  std::shared_ptr<Label> resource_detail_title_label;
  std::shared_ptr<Node> resource_preview_image_container;
  std::shared_ptr<ImageView> resource_preview_image_view;
  std::shared_ptr<Label> resource_preview_image_meta;
  std::shared_ptr<SourceViewer> resource_detail_viewer;

  // Network panel widgets and state.
  bool preserve_network_log = false;
  bool network_this_page_only = true;
  int network_type_filter = 0;
  std::string network_text_filter;
  uint64_t selected_network_entry_id = 0;
  int network_detail_tab = 0;
  std::shared_ptr<Button> network_preserve_btn;
  std::shared_ptr<Button> network_page_only_btn;
  std::shared_ptr<InputBox> network_filter_input;
  std::shared_ptr<NetworkTableDataSource> network_data_source;
  std::shared_ptr<Table> network_table;
  std::shared_ptr<SourceViewer> network_detail_viewer;
  std::shared_ptr<Label> network_footer_label;

  // Document panel widgets and state.
  int document_tree_mode = 0;
  std::shared_ptr<SourceViewer> document_viewer;

  // Page Info panel widgets and state.
  std::string page_info_host;
  std::shared_ptr<Label> pi_title_val;
  std::shared_ptr<Label> pi_url_val;
  std::shared_ptr<Label> pi_host_val;
  std::shared_ptr<Label> pi_mime_val;
  std::shared_ptr<Label> pi_encoding_val;
  std::shared_ptr<Label> pi_quirks_val;
  std::shared_ptr<Label> pi_js_val;
  std::shared_ptr<Label> pi_zoom_val;
  std::shared_ptr<Label> pi_extents_val;
  std::shared_ptr<Label> pi_security_val;
  std::shared_ptr<Label> pi_mixed_val;
  std::shared_ptr<Label> pi_cookies_val;
};

DevToolsState& State() {
  static DevToolsState state;
  return state;
}

void NotifyConsoleListeners() {
  auto& s = State();
  auto listeners_copy = s.console_listeners;
  for (const auto& [id, cb] : listeners_copy) {
    if (cb)
      cb();
  }
}

// Forward declarations of panel refresh functions.
void RefreshActivePanel();
void RefreshConsolePanel();
void RefreshSourcePanel();
void RefreshResourcesPanel();
void RefreshNetworkPanel();
void RefreshDocumentPanel();
void RefreshPageInfoPanel();
void SyncDockPlacement();

// Builds a key-value row for the Page Info panel.
std::shared_ptr<Node> BuildPageInfoRow(std::string_view key,
                                       std::shared_ptr<Label>* out_val_label) {
  return Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
      },
      Label::BasicLabel(
          key,
          [](Layout& layout) {
            layout.SetWidth(kPageInfoKeyColumnWidth);
            layout.SetFlexShrink(0.0f);
          },
          [](Label& label) {
            if (SkFont* bold = GetBold12UiFont())
              label.SetFont(bold);
            label.SetColor(::perception::ui::kSecondaryTextColor);
          }),
      Label::SingleLineTruncated(
          "-", out_val_label,
          [](Layout& layout) { layout.SetFlexGrow(1.0f); }));
}

// Builds the Console panel UI.
std::shared_ptr<Node> BuildConsolePanel() {
  auto& s = State();
  auto clear_icon = Image::LoadImage(kClearAllIconPath);
  auto pin_icon = Image::LoadImage(kPinIconPath);

  auto level_bar = SegmentedOptionBar::Create(
      {"All", "Errors", "Warnings", "Info", "Debug"}, 0, [](int idx) {
        State().console_level_filter = idx;
        RefreshConsolePanel();
      });

  auto action_bar = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetFlexShrink(0.0f);
      },
      ImageButton::BasicImageButton(
          []() { ClearConsoleMessagesForTab(GetActiveTab(), true); },
          clear_icon, Tooltip::ShowTooltip("Clear Console")),
      ImageButton::BasicImageButton(
          []() {
            auto& st = State();
            st.preserve_console_log = !st.preserve_console_log;
            if (st.console_preserve_btn)
              st.console_preserve_btn->SetToggled(st.preserve_console_log);
          },
          pin_icon, &s.console_preserve_btn,
          Tooltip::ShowTooltip("Preserve Log")),
      level_bar->GetNode(),
      Node::Empty([](Layout& layout) { layout.SetFlexGrow(1.0f); }),
      InputBox::BasicInputBox(
          "",
          [](Layout& layout) { layout.SetWidth(kFilterInputWidth); },
          [](InputBox& input) {
            input.OnTextChanged([](std::string_view text) {
              State().console_text_filter = std::string(text);
              RefreshConsolePanel();
            });
          }));

  s.console_messages_container = Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetGap(2.0f);
      });

  auto scroll_area = ScrollContainer::VerticalScrollContainer(
      s.console_messages_container, &s.console_scroll_container,
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetWidthPercent(100.0f);
        layout.SetMinHeight(0.0f);
      });

  auto run_js = []() {
    auto& st = State();
    if (!st.console_js_input)
      return;
    std::string code = st.console_js_input->GetText();
    if (code.empty())
      return;
    st.console_js_input->SetText("");

    Window* gw = GetActiveTab();
    if (!gw || !gw->GetBrowserWindow())
      return;

    AppendConsoleMessage(gw, BW_CS_INPUT, code.data(), code.size(),
                         BW_CS_FLAG_LEVEL_LOG);

    if (!nsoption_bool(enable_javascript)) {
      constexpr std::string_view kJsDisabledMsg =
          "JavaScript execution is disabled in Settings.";
      AppendConsoleMessage(gw, BW_CS_SCRIPT_ERROR, kJsDisabledMsg.data(),
                           kJsDisabledMsg.size(), BW_CS_FLAG_LEVEL_WARN);
      return;
    }

    bool ok = false;
    {
      NETSURF_LOCK;
      ok = browser_window_exec(gw->GetBrowserWindow(), code.data(),
                               code.size());
      browser_window_schedule_reformat(gw->GetBrowserWindow());
    }
    if (!ok) {
      constexpr std::string_view kExecErrMsg =
          "JavaScript evaluation failed or returned an uncaught error.";
      AppendConsoleMessage(gw, BW_CS_SCRIPT_ERROR, kExecErrMsg.data(),
                           kExecErrMsg.size(), BW_CS_FLAG_LEVEL_ERROR);
    }
    if (gw->GetContentNode())
      gw->GetContentNode()->Invalidate();
    if (st.active_panel == DevToolsPanel::Document)
      RefreshDocumentPanel();
  };

  auto js_input_row = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetFlexShrink(0.0f);
      },
      Label::BasicLabel(">", [](Label& label) {
        if (SkFont* mono = GetMonospace12UiFont())
          label.SetFont(mono);
        label.SetColor(kConsoleInputTextColor);
      }),
      InputBox::BasicInputBox(
          "", &s.console_js_input,
          [](Layout& layout) { layout.SetFlexGrow(1.0f); },
          [run_js](InputBox& input) {
            if (SkFont* mono = GetMonospace12UiFont())
              input.SetFont(mono);
            input.OnEnterPressed([run_js](std::string_view) { run_js(); });
          }),
      Button::TextButton("Run", run_js));

  return Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetWidthPercent(100.0f);
        layout.SetMinHeight(0.0f);
      },
      action_bar, scroll_area, js_input_row);
}

// Builds the Source panel UI.
std::shared_ptr<Node> BuildSourcePanel() {
  auto& s = State();
  auto refresh_icon = Image::LoadImage(kRefreshIconPath);

  std::vector<std::string> initial_options = {"(Main Document)"};
  auto combo_node = ComboBox::BasicComboBox(
      initial_options, 0,
      [](int idx) {
        auto& st = State();
        st.selected_source_index = idx;
        if (idx >= 0 && idx < static_cast<int>(st.source_resources.size())) {
          const auto& res = st.source_resources[idx];
          std::string text = ReadResourceText(res);
          if (text.empty())
            text = "// No source content available for " + res.url;
          st.source_viewer->SetContent(
              text, res.name.empty() ? "source.txt" : res.name);
        }
      },
      &s.source_combo_box, [](Layout& layout) { layout.SetFlexGrow(1.0f); });

  auto action_bar = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetFlexShrink(0.0f);
      },
      Label::BasicLabel("Resource:"), combo_node,
      ImageButton::BasicImageButton([]() { RefreshSourcePanel(); },
                                    refresh_icon,
                                    Tooltip::ShowTooltip("Refresh Sources")));

  s.source_viewer = SourceViewer::Create(true);

  return Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetWidthPercent(100.0f);
        layout.SetMinHeight(0.0f);
      },
      action_bar, s.source_viewer->GetRootNode());
}

// Updates the bottom preview pane of the Resources panel for the selected resource.
void UpdateSelectedResourcePreview() {
  auto& s = State();
  const auto& displayed = s.resources_data_source
                              ? s.resources_data_source->GetResources()
                              : s.all_page_resources;
  if (s.selected_resource_index < 0 ||
      s.selected_resource_index >= static_cast<int>(displayed.size())) {
    if (s.resource_detail_title_label)
      s.resource_detail_title_label->SetText("Select a resource to inspect");
    if (s.resource_preview_image_container)
      s.resource_preview_image_container->GetLayout().SetDisplay(YGDisplayNone);
    if (s.resource_detail_viewer) {
      s.resource_detail_viewer->GetRootNode()->GetLayout().SetDisplay(
          YGDisplayFlex);
      s.resource_detail_viewer->SetContent("", "resource.txt");
    }
    return;
  }

  const PageResource& res = displayed[s.selected_resource_index];
  if (s.resource_detail_title_label)
    s.resource_detail_title_label->SetText(
        std::string(ResourceCategoryToString(res.category)) + ": " +
        (res.url.empty() ? res.name : res.url));

  bool is_image = (res.category == ResourceCategory::kImage ||
                   res.category == ResourceCategory::kBackgroundImage ||
                   ContainsIgnoreCase(res.mime_type, "image/")) &&
                  !ContainsIgnoreCase(res.mime_type, "svg");

  if (is_image && res.handle != nullptr) {
    std::shared_ptr<Image> preview_img;
    {
      NETSURF_LOCK;
      struct bitmap* bm = content_get_bitmap(res.handle);
      if (bm != nullptr && !bm->sk_bitmap.empty()) {
        if (!bm->cached_image)
          bm->cached_image = SkImages::RasterFromBitmap(bm->sk_bitmap);
        if (bm->cached_image)
          preview_img = Image::FromSkImage(bm->cached_image);
      }
    }
    if (preview_img) {
      if (s.resource_preview_image_view)
        s.resource_preview_image_view->SetImage(preview_img);
      if (s.resource_preview_image_meta) {
        std::string meta =
            "Dimensions: " + std::to_string(res.width) + " × " +
            std::to_string(res.height) + " px   ·   MIME: " +
            (res.mime_type.empty() ? "image" : res.mime_type) +
            "   ·   Size: " + FormatByteSize(res.size_bytes);
        s.resource_preview_image_meta->SetText(meta);
      }
      if (s.resource_preview_image_container)
        s.resource_preview_image_container->GetLayout().SetDisplay(
            YGDisplayFlex);
      if (s.resource_detail_viewer)
        s.resource_detail_viewer->GetRootNode()->GetLayout().SetDisplay(
            YGDisplayNone);
      if (s.root_node)
        s.root_node->Invalidate();
      return;
    }
  }

  if (s.resource_preview_image_container)
    s.resource_preview_image_container->GetLayout().SetDisplay(YGDisplayNone);
  if (s.resource_detail_viewer) {
    s.resource_detail_viewer->GetRootNode()->GetLayout().SetDisplay(
        YGDisplayFlex);
    std::string text = ReadResourceText(res);
    if (text.empty())
      text = "URL: " + res.url + "\nType: " +
             std::string(ResourceCategoryToString(res.category)) + "\nMIME: " +
             res.mime_type + "\nSize: " + FormatByteSize(res.size_bytes) +
             "\nStatus: " + res.status;
    s.resource_detail_viewer->SetContent(
        text, res.name.empty() ? "resource.txt" : res.name);
  }
  if (s.root_node)
    s.root_node->Invalidate();
}

// Builds the Resources panel UI.
std::shared_ptr<Node> BuildResourcesPanel() {
  auto& s = State();
  auto refresh_icon = Image::LoadImage(kRefreshIconPath);
  auto open_icon = Image::LoadImage(kExternalLinkIconPath);
  auto copy_icon = Image::LoadImage(kCopyIconPath);
  auto save_icon = Image::LoadImage(kSaveIconPath);
  auto search_icon = Image::LoadImage(kSearchIconPath);

  auto action_bar = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetFlexShrink(0.0f);
      },
      Label::SingleLineTruncated(
          "0 resources", &s.resources_summary_label,
          [](Layout& layout) { layout.SetFlexGrow(1.0f); }),
      ImageButton::BasicImageButton([]() { RefreshResourcesPanel(); },
                                    refresh_icon,
                                    Tooltip::ShowTooltip("Refresh Resources")),
      InputBox::BasicInputBox(
          "",
          [](Layout& layout) { layout.SetWidth(kFilterInputWidth); },
          [](InputBox& input) {
            input.OnTextChanged([](std::string_view text) {
              State().resources_filter = std::string(text);
              RefreshResourcesPanel();
            });
          }));

  s.resources_data_source = std::make_shared<ResourcesTableDataSource>();
  std::vector<Table::Column> columns = {
      {.title = "Type",
       .layout_modifier = [](Layout& l) { l.SetWidth(kResColTypeWidth); }},
      {.title = "Name",
       .layout_modifier = [](Layout& l) { l.SetWidth(kResColNameWidth); }},
      {.title = "URL",
       .layout_modifier = [](Layout& l) { l.SetFlexGrow(1.0f); }},
      {.title = "MIME Type",
       .layout_modifier = [](Layout& l) { l.SetWidth(kResColMimeWidth); }},
      {.title = "Size",
       .layout_modifier = [](Layout& l) { l.SetWidth(kResColSizeWidth); }},
      {.title = "Status",
       .layout_modifier = [](Layout& l) { l.SetWidth(kResColStatusWidth); }},
      {.title = "Dimensions",
       .layout_modifier = [](Layout& l) { l.SetWidth(kResColDimsWidth); }},
  };

  auto table_node = Table::BasicTable(
      s.resources_data_source, columns, &s.resources_table,
      [](ResizableContainerItem& item) {
        item.SetBehavior(ResizableContainerItem::Behavior::Flex);
      },
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetWidthPercent(100.0f);
      },
      [](Table& table) {
        table.OnCellSelect([](int row, int) {
          State().selected_resource_index = row;
          UpdateSelectedResourcePreview();
        });
      });

  auto detail_toolbar = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetFlexShrink(0.0f);
      },
      Label::SingleLineTruncated(
          "Select a resource to inspect", &s.resource_detail_title_label,
          [](Layout& layout) { layout.SetFlexGrow(1.0f); },
          [](Label& label) {
            if (SkFont* bold = GetBold12UiFont())
              label.SetFont(bold);
          }),
      ImageButton::BasicImageButton(
          []() {
            auto& st = State();
            if (!st.resources_data_source)
              return;
            const auto& list = st.resources_data_source->GetResources();
            if (st.selected_resource_index < 0 ||
                st.selected_resource_index >= static_cast<int>(list.size()))
              return;
            const std::string& url_str = list[st.selected_resource_index].url;
            if (url_str.empty())
              return;
            Window* gw = GetActiveTab();
            if (!gw || !gw->GetBrowserWindow())
              return;
            NETSURF_LOCK;
            struct nsurl* url = nullptr;
            if (nsurl_create(url_str.c_str(), &url) == NSERROR_OK && url) {
              struct browser_window* new_bw = nullptr;
              browser_window_create(
                  static_cast<browser_window_create_flags>(
                      BW_CREATE_TAB | BW_CREATE_HISTORY | BW_CREATE_FOREGROUND),
                  url, nullptr, gw->GetBrowserWindow(), &new_bw);
              nsurl_unref(url);
            }
          },
          open_icon, Tooltip::ShowTooltip("Open Resource in New Tab")),
      ImageButton::BasicImageButton(
          []() {
            auto& st = State();
            if (!st.resources_data_source)
              return;
            const auto& list = st.resources_data_source->GetResources();
            if (st.selected_resource_index < 0 ||
                st.selected_resource_index >= static_cast<int>(list.size()))
              return;
            ::perception::SetClipboard(
                std::string_view(list[st.selected_resource_index].url));
          },
          copy_icon, Tooltip::ShowTooltip("Copy Resource URL")),
      ImageButton::BasicImageButton(
          []() {
            auto& st = State();
            if (!st.resources_data_source)
              return;
            const auto& list = st.resources_data_source->GetResources();
            if (st.selected_resource_index < 0 ||
                st.selected_resource_index >= static_cast<int>(list.size()))
              return;
            const PageResource res = list[st.selected_resource_index];
            std::string raw_bytes;
            if (!res.inline_source.empty()) {
              raw_bytes = res.inline_source;
            } else if (res.handle != nullptr) {
              NETSURF_LOCK;
              size_t sz = 0;
              const auto* ptr = content_get_source_data(res.handle, &sz);
              if (ptr != nullptr && sz > 0)
                raw_bytes.assign(reinterpret_cast<const char*>(ptr), sz);
            }
            std::string default_name =
                res.name.empty() ? ExtractUrlLeafName(res.url) : res.name;
            ShowSaveFileDialog(
                [raw_bytes](bool succeeded, std::string_view path) {
                  if (!succeeded || path.empty())
                    return;
                  std::string path_str(path);
                  FILE* f = fopen(path_str.c_str(), "wb");
                  if (!f)
                    return;
                  if (!raw_bytes.empty())
                    fwrite(raw_bytes.data(), 1, raw_bytes.size(), f);
                  fclose(f);
                },
                {}, default_name, "", "Save Resource As", GetGlobalUiWindow());
          },
          save_icon, Tooltip::ShowTooltip("Save Resource As...")),
      ImageButton::BasicImageButton(
          []() {
            auto& st = State();
            if (!st.resources_data_source)
              return;
            const auto& list = st.resources_data_source->GetResources();
            if (st.selected_resource_index < 0 ||
                st.selected_resource_index >= static_cast<int>(list.size()))
              return;
            std::string target_url = list[st.selected_resource_index].url;
            st.network_text_filter = target_url;
            if (st.network_filter_input)
              st.network_filter_input->SetText(target_url);
            OpenDevTools(DevToolsPanel::Network);
          },
          search_icon, Tooltip::ShowTooltip("Show in Network Panel")));

  s.resource_preview_image_container = Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetFlexGrow(1.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetJustifyContent(YGJustifyCenter);
        layout.SetDisplay(YGDisplayNone);
      },
      ImageView::BasicImage(
          nullptr, &s.resource_preview_image_view,
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetMaxHeight(kImagePreviewMaxHeight);
            layout.SetFlexGrow(1.0f);
          },
          [](ImageView& iv) {
            iv.SetResizeMethod(ResizeMethod::Contain);
            iv.SetAlignment(TextAlignment::MiddleCenter);
          }),
      Label::SingleLineTruncated("", &s.resource_preview_image_meta,
                                 [](Label& label) {
                                   label.SetColor(
                                       ::perception::ui::kSecondaryTextColor);
                                 }));

  s.resource_detail_viewer = SourceViewer::Create(true);

  auto bottom_pane = Container::VerticalContainer(
      [](ResizableContainerItem& item) {
        item.SetBehavior(ResizableContainerItem::Behavior::Flex);
      },
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetWidthPercent(100.0f);
        layout.SetMinHeight(0.0f);
      },
      detail_toolbar, s.resource_preview_image_container,
      s.resource_detail_viewer->GetRootNode());

  auto split = ResizableContainer::VerticalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetWidthPercent(100.0f);
        layout.SetMinHeight(0.0f);
      },
      table_node, bottom_pane);

  return Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetWidthPercent(100.0f);
        layout.SetMinHeight(0.0f);
      },
      action_bar, split);
}

bool StartsWithHttps(std::string_view url) {
  return url.size() >= 8 && ContainsIgnoreCase(url.substr(0, 8), "https://");
}

// Updates the bottom details pane of the Network panel for the selected entry.
void UpdateSelectedNetworkEntryDetails() {
  auto& s = State();
  if (!s.network_detail_viewer || !s.network_data_source)
    return;

  const auto& entries = s.network_data_source->GetEntries();
  const NetworkLogEntry* selected = nullptr;
  for (const auto& e : entries) {
    if (e.id == s.selected_network_entry_id) {
      selected = &e;
      break;
    }
  }
  if (!selected && !entries.empty()) {
    selected = &entries.front();
    s.selected_network_entry_id = selected->id;
  }
  if (!selected) {
    s.network_detail_viewer->SetContent(
        "Select a network request above to inspect headers, response, timing, "
        "and security details.",
        "network.txt");
    return;
  }

  std::string content;
  if (s.network_detail_tab == 0) {
    // Headers tab.
    content += "=== General ===\n";
    content += "Request URL: " + selected->url + "\n";
    content += "Request Method: " +
               (selected->method.empty() ? "GET" : selected->method) + "\n";
    content += "Status: " + net_compat::GetStatusString(*selected) + "\n";
    std::string redir = net_compat::GetRedirectUrl(*selected);
    if (!redir.empty())
      content += "Redirect Target: " + redir + "\n";
    std::string mime = net_compat::GetMimeType(*selected);
    if (!mime.empty())
      content += "Content-Type: " + mime + "\n";
    content +=
        "Bytes Received: " + FormatByteSize(selected->bytes_received) + "\n";

    content += "\n=== Request Headers ===\n";
    std::string req_hdrs = net_compat::FormatRequestHeaders(*selected);
    content += req_hdrs.empty() ? "(Not recorded for this scheme)\n" : req_hdrs;

    content += "\n=== Response Headers ===\n";
    if (selected->response_headers.empty()) {
      content += "(No response headers)\n";
    } else {
      for (const auto& h : selected->response_headers)
        content += h + "\n";
    }
  } else if (s.network_detail_tab == 1) {
    // Response tab.
    Window* gw = GetActiveTab();
    if (gw && gw->GetBrowserWindow()) {
      auto resources = CollectPageResources(gw->GetBrowserWindow());
      std::string redir = net_compat::GetRedirectUrl(*selected);
      for (const auto& res : resources) {
        if (res.url == selected->url ||
            (!redir.empty() && res.url == redir)) {
          content = ReadResourceText(res);
          break;
        }
      }
    }
    if (content.empty())
      content =
          "// Response body preview is available for cached document, CSS, and "
          "script resources.\nURL: " +
          selected->url + "\nReceived: " +
          FormatByteSize(selected->bytes_received);
  } else if (s.network_detail_tab == 2) {
    // Timing tab.
    int64_t start = static_cast<int64_t>(selected->start_time_ms);
    int64_t first_byte = net_compat::GetFirstByteTimeMs(*selected);
    int64_t end = static_cast<int64_t>(selected->end_time_ms);
    int64_t total_dur = net_compat::GetDurationMs(*selected);
    int64_t ttfb_delta =
        (first_byte > start && start > 0) ? (first_byte - start) : 0;
    int64_t download_delta =
        (end > first_byte && first_byte > 0) ? (end - first_byte) : total_dur;

    content += "=== Request Timing ===\n";
    content += "Start Time:           " + std::to_string(start) + " ms\n";
    content += "Time to First Byte:   " + std::to_string(ttfb_delta) + " ms\n";
    content +=
        "Content Download:     " + std::to_string(download_delta) + " ms\n";
    content += "Total Duration:       " + std::to_string(total_dur) + " ms\n";
    std::string err = net_compat::GetErrorMessage(*selected);
    if (!err.empty())
      content += "Status / Message:     " + err + "\n";
  } else {
    // Security tab.
    content += "=== Connection Security ===\n";
    content += "Scheme: " +
               (selected->scheme.empty() ? "unknown" : selected->scheme) + "\n";
    if (ContainsIgnoreCase(selected->scheme, "https") ||
        StartsWithHttps(selected->url)) {
      content += "Transport: Encrypted (TLS / HTTPS)\n\n";
      content += selected->tls_info.empty()
                     ? "TLS session negotiated via BearSSL."
                     : selected->tls_info;
    } else {
      content +=
          "Transport: Unencrypted (" + selected->url + ")\n"
          "This request was not transferred over TLS.";
    }
  }

  s.network_detail_viewer->SetContent(content, "network-details.txt");
}

// Builds the Network panel UI.
std::shared_ptr<Node> BuildNetworkPanel() {
  auto& s = State();
  auto clear_icon = Image::LoadImage(kClearAllIconPath);
  auto pin_icon = Image::LoadImage(kPinIconPath);
  auto filter_icon = Image::LoadImage(kFilterIconPath);
  auto copy_icon = Image::LoadImage(kCopyIconPath);

  auto type_bar = SegmentedOptionBar::Create(
      {"All", "Doc", "CSS", "JS", "Img", "Other"}, 0, [](int idx) {
        State().network_type_filter = idx;
        RefreshNetworkPanel();
      });

  auto page_only_node = ImageButton::BasicImageButton(
      []() {
        auto& st = State();
        st.network_this_page_only = !st.network_this_page_only;
        if (st.network_page_only_btn)
          st.network_page_only_btn->SetToggled(st.network_this_page_only);
        RefreshNetworkPanel();
      },
      filter_icon, &s.network_page_only_btn,
      Tooltip::ShowTooltip("This Page Only"));
  if (s.network_page_only_btn)
    s.network_page_only_btn->SetToggled(true);

  auto action_bar = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetFlexShrink(0.0f);
      },
      ImageButton::BasicImageButton(
          []() {
            ClearNetworkLog();
            RefreshNetworkPanel();
          },
          clear_icon, Tooltip::ShowTooltip("Clear Network Log")),
      ImageButton::BasicImageButton(
          []() {
            auto& st = State();
            st.preserve_network_log = !st.preserve_network_log;
            if (st.network_preserve_btn)
              st.network_preserve_btn->SetToggled(st.preserve_network_log);
          },
          pin_icon, &s.network_preserve_btn,
          Tooltip::ShowTooltip("Preserve Log")),
      page_only_node, type_bar->GetNode(),
      Node::Empty([](Layout& layout) { layout.SetFlexGrow(1.0f); }),
      InputBox::BasicInputBox(
          "", &s.network_filter_input,
          [](Layout& layout) { layout.SetWidth(kFilterInputWidth); },
          [](InputBox& input) {
            input.OnTextChanged([](std::string_view text) {
              State().network_text_filter = std::string(text);
              RefreshNetworkPanel();
            });
          }));

  s.network_data_source = std::make_shared<NetworkTableDataSource>();
  std::vector<Table::Column> columns = {
      {.title = "#",
       .layout_modifier = [](Layout& l) { l.SetWidth(kNetColIndexWidth); }},
      {.title = "Method",
       .layout_modifier = [](Layout& l) { l.SetWidth(kNetColMethodWidth); }},
      {.title = "Status",
       .layout_modifier = [](Layout& l) { l.SetWidth(kNetColStatusWidth); }},
      {.title = "Name",
       .layout_modifier = [](Layout& l) { l.SetWidth(kNetColNameWidth); }},
      {.title = "URL",
       .layout_modifier = [](Layout& l) { l.SetFlexGrow(1.0f); }},
      {.title = "Type",
       .layout_modifier = [](Layout& l) { l.SetWidth(kNetColTypeWidth); }},
      {.title = "Size",
       .layout_modifier = [](Layout& l) { l.SetWidth(kNetColSizeWidth); }},
      {.title = "Time",
       .layout_modifier = [](Layout& l) { l.SetWidth(kNetColTimeWidth); }},
      {.title = "Waterfall",
       .layout_modifier = [](Layout& l) { l.SetWidth(kNetColWaterfallWidth); }},
  };

  auto table_node = Table::BasicTable(
      s.network_data_source, columns, &s.network_table,
      [](ResizableContainerItem& item) {
        item.SetBehavior(ResizableContainerItem::Behavior::Flex);
      },
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetWidthPercent(100.0f);
      },
      [](Table& table) {
        table.OnCellSelect([](int row, int) {
          auto& st = State();
          if (!st.network_data_source)
            return;
          const auto& list = st.network_data_source->GetEntries();
          if (row >= 0 && row < static_cast<int>(list.size())) {
            st.selected_network_entry_id = list[row].id;
            UpdateSelectedNetworkEntryDetails();
          }
        });
      });

  auto detail_tabs = SegmentedOptionBar::Create(
      {"Headers", "Response", "Timing", "Security"}, 0, [](int idx) {
        State().network_detail_tab = idx;
        UpdateSelectedNetworkEntryDetails();
      });

  auto detail_header = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetFlexShrink(0.0f);
      },
      detail_tabs->GetNode(),
      Node::Empty([](Layout& layout) { layout.SetFlexGrow(1.0f); }),
      ImageButton::BasicImageButton(
          []() {
            auto& st = State();
            if (!st.network_data_source)
              return;
            for (const auto& e : st.network_data_source->GetEntries()) {
              if (e.id == st.selected_network_entry_id) {
                ::perception::SetClipboard(std::string_view(e.url));
                break;
              }
            }
          },
          copy_icon, Tooltip::ShowTooltip("Copy Request URL")));

  s.network_detail_viewer = SourceViewer::Create(false);

  auto bottom_pane = Container::VerticalContainer(
      [](ResizableContainerItem& item) {
        item.SetBehavior(ResizableContainerItem::Behavior::Flex);
      },
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetWidthPercent(100.0f);
        layout.SetMinHeight(0.0f);
      },
      detail_header, s.network_detail_viewer->GetRootNode());

  auto split = ResizableContainer::VerticalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetWidthPercent(100.0f);
        layout.SetMinHeight(0.0f);
      },
      table_node, bottom_pane);

  return Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetWidthPercent(100.0f);
        layout.SetMinHeight(0.0f);
      },
      action_bar, split,
      Label::SingleLineTruncated(
          "0 requests · 0 B transferred", &s.network_footer_label,
          [](Label& label) {
            label.SetColor(::perception::ui::kSecondaryTextColor);
          }));
}

// Builds the Document (DOM / Box Tree) panel UI.
std::shared_ptr<Node> BuildDocumentPanel() {
  auto& s = State();
  auto refresh_icon = Image::LoadImage(kRefreshIconPath);

  auto mode_bar = SegmentedOptionBar::Create(
      {"DOM Tree", "Box / Layout Tree"}, 0, [](int idx) {
        State().document_tree_mode = idx;
        RefreshDocumentPanel();
      });

  auto action_bar = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetFlexShrink(0.0f);
      },
      mode_bar->GetNode(),
      ImageButton::BasicImageButton([]() { RefreshDocumentPanel(); },
                                    refresh_icon,
                                    Tooltip::ShowTooltip("Refresh Tree")),
      Node::Empty([](Layout& layout) { layout.SetFlexGrow(1.0f); }));

  s.document_viewer = SourceViewer::Create(true);

  return Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetWidthPercent(100.0f);
        layout.SetMinHeight(0.0f);
      },
      action_bar, s.document_viewer->GetRootNode());
}

// Builds the Page Info panel UI.
std::shared_ptr<Node> BuildPageInfoPanel() {
  auto& s = State();
  auto cookie_icon = Image::LoadImage(kCookieIconPath);
  auto refresh_icon = Image::LoadImage(kRefreshIconPath);

  auto action_bar = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetFlexShrink(0.0f);
      },
      Label::BasicLabel("Page & Security Summary", [](Label& label) {
        if (SkFont* bold = GetBold12UiFont())
          label.SetFont(bold);
      }),
      Node::Empty([](Layout& layout) { layout.SetFlexGrow(1.0f); }),
      ImageButton::BasicImageButton(
          []() {
            const std::string& host = State().page_info_host;
            ShowCookieManagerWindow(host.empty() ? nullptr : host.c_str());
          },
          cookie_icon,
          Tooltip::ShowTooltip("Open Cookie Manager for Host")),
      ImageButton::BasicImageButton([]() { RefreshPageInfoPanel(); },
                                    refresh_icon,
                                    Tooltip::ShowTooltip("Refresh Page Info")));

  auto info_rows = Container::VerticalContainer(
      [](Layout& layout) { layout.SetWidthPercent(100.0f); },
      BuildPageInfoRow("Title", &s.pi_title_val),
      BuildPageInfoRow("URL", &s.pi_url_val),
      BuildPageInfoRow("Host", &s.pi_host_val),
      BuildPageInfoRow("MIME Type", &s.pi_mime_val),
      BuildPageInfoRow("Character Encoding", &s.pi_encoding_val),
      BuildPageInfoRow("Rendering Mode", &s.pi_quirks_val),
      BuildPageInfoRow("JavaScript", &s.pi_js_val),
      BuildPageInfoRow("Zoom Scale", &s.pi_zoom_val),
      BuildPageInfoRow("Document Extents", &s.pi_extents_val),
      BuildPageInfoRow("Security State", &s.pi_security_val),
      BuildPageInfoRow("Mixed Content", &s.pi_mixed_val),
      BuildPageInfoRow("Cookies in Use", &s.pi_cookies_val));

  auto scroll = ScrollContainer::VerticalScrollContainer(
      info_rows, [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetWidthPercent(100.0f);
        layout.SetMinHeight(0.0f);
      });

  return Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetWidthPercent(100.0f);
        layout.SetMinHeight(0.0f);
      },
      action_bar, scroll);
}

// Ensures the Developer Tools root hierarchy is constructed once.
void EnsureDevToolsInitialized() {
  auto& s = State();
  if (s.is_initialized)
    return;
  s.is_initialized = true;

  s.popout_icon = Image::LoadImage(kExternalLinkIconPath);
  s.dock_icon = Image::LoadImage(kDockBottomIconPath);
  auto close_icon = Image::LoadImage(kCloseIconPath);

  s.main_tab_bar = SegmentedOptionBar::Create(
      {"Console", "Source", "Resources", "Network", "Document", "Page Info"},
      0, [](int idx) {
        auto& st = State();
        st.active_panel = static_cast<DevToolsPanel>(idx);
        for (int i = 0; i < 6; ++i) {
          if (st.panel_nodes[i])
            st.panel_nodes[i]->GetLayout().SetDisplay(
                i == idx ? YGDisplayFlex : YGDisplayNone);
        }
        RefreshActivePanel();
        if (st.root_node)
          st.root_node->Invalidate();
      });

  s.dock_toggle_node = ImageButton::BasicImageButton(
      []() { SetDevToolsDocked(!IsDevToolsDocked()); }, s.popout_icon,
      &s.dock_toggle_image_button,
      Tooltip::ShowTooltip("Undock into Separate Window"));

  s.close_button_node = ImageButton::BasicImageButton(
      []() { CloseDevTools(); }, close_icon,
      Tooltip::ShowTooltip("Close Developer Tools"));

  auto header_bar = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetFlexShrink(0.0f);
      },
      s.main_tab_bar->GetNode(),
      Node::Empty([](Layout& layout) { layout.SetFlexGrow(1.0f); }),
      s.dock_toggle_node, s.close_button_node);

  auto divider = Container::HorizontalContainer(
      [](Block& block) {
        block.SetFillColor(::perception::ui::kTableDividerColor);
      },
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetHeight(1.0f);
        layout.SetFlexShrink(0.0f);
      });

  s.panel_nodes[0] = BuildConsolePanel();
  s.panel_nodes[1] = BuildSourcePanel();
  s.panel_nodes[2] = BuildResourcesPanel();
  s.panel_nodes[3] = BuildNetworkPanel();
  s.panel_nodes[4] = BuildDocumentPanel();
  s.panel_nodes[5] = BuildPageInfoPanel();

  for (int i = 0; i < 6; ++i) {
    s.panel_nodes[i]->GetLayout().SetDisplay(
        i == static_cast<int>(s.active_panel) ? YGDisplayFlex : YGDisplayNone);
  }

  s.root_node = Container::VerticalContainer(
      [](Block& block) {
        block.SetFillColor(::perception::ui::kBackgroundWindowColor);
        block.SetBorderColor(::perception::ui::kContainerBorderColor);
        block.SetBorderWidth(1.0f);
      },
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetWidthPercent(100.0f);
        layout.SetMinHeight(0.0f);
        layout.SetPadding(YGEdgeAll, ::perception::ui::kContainerPadding);
      },
      header_bar, divider, s.panel_nodes[0], s.panel_nodes[1],
      s.panel_nodes[2], s.panel_nodes[3], s.panel_nodes[4], s.panel_nodes[5]);

  s.docked_host_node = Container::VerticalContainer(
      [](ResizableContainerItem& item) {
        item.SetBehavior(ResizableContainerItem::Behavior::Fixed);
      },
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetHeight(kDefaultDockedHeight);
        layout.SetMinHeight(kMinDockedHeight);
        layout.SetGap(0.0f);
        layout.SetPadding(YGEdgeAll, 0.0f);
        layout.SetDisplay(YGDisplayNone);
      });

  AddNetworkLogListener([]() {
    auto& st = State();
    if (st.is_open && st.active_panel == DevToolsPanel::Network)
      RefreshNetworkPanel();
  });
}

// Formats a millisecond timestamp as [SS.mmms] for Console display.
std::string FormatConsoleTimestamp(int64_t ts_ms) {
  int64_t secs = (ts_ms / 1000) % 10000;
  int64_t millis = ts_ms % 1000;
  char buf[32];
  snprintf(buf, sizeof(buf), "%04lld.%03llds",
           static_cast<long long>(secs), static_cast<long long>(millis));
  return buf;
}

// Rebuilds the visible message rows in the Console panel.
void RefreshConsolePanel() {
  auto& s = State();
  if (!s.console_messages_container)
    return;

  s.console_messages_container->RemoveChildren();
  Window* gw = GetActiveTab();
  if (!gw)
    return;

  auto it = s.tab_logs.find(gw);
  if (it == s.tab_logs.end() || it->second.empty()) {
    s.console_messages_container->AddChild(Label::BasicLabel(
        "No console messages.", [](Label& label) {
          label.SetColor(::perception::ui::kSecondaryTextColor);
        }));
    s.console_messages_container->Invalidate();
    return;
  }

  auto err_icon = Image::LoadImage(kAlertCircleIconPath);
  auto warn_icon = Image::LoadImage(kAlertTriangleIconPath);

  for (const auto& msg : it->second) {
    if (s.console_level_filter == 1 && msg.level != ConsoleLevel::Error)
      continue;
    if (s.console_level_filter == 2 && msg.level != ConsoleLevel::Warn)
      continue;
    if (s.console_level_filter == 3 &&
        msg.level != ConsoleLevel::Info && msg.level != ConsoleLevel::Log)
      continue;
    if (s.console_level_filter == 4 && msg.level != ConsoleLevel::Debug)
      continue;
    if (!ContainsIgnoreCase(msg.text, s.console_text_filter))
      continue;

    uint32_t bg_color = 0;
    uint32_t text_color = ::perception::ui::kLabelTextColor;
    std::shared_ptr<Node> icon_node;

    if (msg.source == BW_CS_INPUT) {
      text_color = kConsoleInputTextColor;
      icon_node = Label::BasicLabel(">", [](Label& l) {
        if (SkFont* mono = GetMonospace12UiFont())
          l.SetFont(mono);
        l.SetColor(kConsoleInputTextColor);
      });
    } else if (msg.level == ConsoleLevel::Error) {
      bg_color = kConsoleErrorRowBgColor;
      text_color = kConsoleErrorTextColor;
      icon_node = ImageView::BasicImage(
          err_icon,
          [](Layout& l) {
            l.SetWidth(kConsoleIconSize);
            l.SetHeight(kConsoleIconSize);
          },
          [](ImageView& iv) {
            iv.SetResizeMethod(ResizeMethod::Contain);
            iv.SetColor(kConsoleErrorIconTint);
          });
    } else if (msg.level == ConsoleLevel::Warn) {
      bg_color = kConsoleWarnRowBgColor;
      text_color = kConsoleWarnTextColor;
      icon_node = ImageView::BasicImage(
          warn_icon,
          [](Layout& l) {
            l.SetWidth(kConsoleIconSize);
            l.SetHeight(kConsoleIconSize);
          },
          [](ImageView& iv) {
            iv.SetResizeMethod(ResizeMethod::Contain);
            iv.SetColor(kConsoleWarnIconTint);
          });
    } else {
      if (msg.level == ConsoleLevel::Debug)
        text_color = kConsoleDebugTextColor;
      icon_node = Node::Empty([](Layout& l) {
        l.SetWidth(kConsoleIconSize);
        l.SetHeight(kConsoleIconSize);
      });
    }

    std::string_view src_tag = "[Log]";
    if (msg.source == BW_CS_INPUT)
      src_tag = "[Input]";
    else if (msg.source == BW_CS_SCRIPT_ERROR)
      src_tag = "[Error]";
    else if (msg.level == ConsoleLevel::Warn)
      src_tag = "[Warn]";
    else if (msg.level == ConsoleLevel::Debug)
      src_tag = "[Debug]";
    else if (msg.level == ConsoleLevel::Info)
      src_tag = "[Info]";

    std::string copy_text = msg.text;
    auto row = Container::HorizontalContainer(
        [bg_color](Block& block) {
          if (bg_color != 0) {
            block.SetFillColor(bg_color);
            block.SetBorderRadius(4.0f);
          }
        },
        [](Layout& layout) {
          layout.SetWidthPercent(100.0f);
          layout.SetAlignItems(YGAlignFlexStart);
          layout.SetPadding(YGEdgeHorizontal, 4.0f);
          layout.SetPadding(YGEdgeVertical, 2.0f);
        },
        [copy_text](Node& node) {
          node.OnMouseButtonUp(
              [copy_text](const ::perception::ui::Point&,
                          ::perception::window::MouseButton button) {
                if (button == ::perception::window::MouseButton::Left)
                  ::perception::SetClipboard(std::string_view(copy_text));
              });
        },
        Tooltip::ShowTooltip("Click to copy message"),
        Label::BasicLabel(
            FormatConsoleTimestamp(msg.timestamp_ms),
            [](Layout& l) {
              l.SetWidth(kConsoleTimestampWidth);
              l.SetFlexShrink(0.0f);
            },
            [](Label& l) {
              if (SkFont* mono = GetMonospace12UiFont())
                l.SetFont(mono);
              l.SetColor(::perception::ui::kSecondaryTextColor);
            }),
        icon_node,
        Label::BasicLabel(
            src_tag,
            [](Layout& l) {
              l.SetWidth(kConsoleSourceWidth);
              l.SetFlexShrink(0.0f);
            },
            [](Label& l) {
              if (SkFont* mono = GetMonospace12UiFont())
                l.SetFont(mono);
              l.SetColor(::perception::ui::kSecondaryTextColor);
            }),
        Label::BasicLabel(
            msg.text,
            [](Layout& l) {
              l.SetFlexGrow(1.0f);
              l.SetFlexShrink(1.0f);
            },
            [text_color](Label& l) {
              if (SkFont* mono = GetMonospace12UiFont())
                l.SetFont(mono);
              l.SetColor(text_color);
            }));

    s.console_messages_container->AddChild(row);
  }

  s.console_messages_container->Invalidate();
}

// Rebuilds the Source panel resource selector and loads the selected source.
void RefreshSourcePanel() {
  auto& s = State();
  if (!s.source_viewer || !s.source_combo_box)
    return;

  s.source_resources.clear();
  Window* gw = GetActiveTab();
  if (gw && gw->GetBrowserWindow()) {
    auto all_res = CollectPageResources(gw->GetBrowserWindow());
    for (const auto& r : all_res) {
      if (IsTextResource(r))
        s.source_resources.push_back(r);
    }
  }

  std::vector<std::string> options;
  for (const auto& r : s.source_resources) {
    std::string label = std::string(ResourceCategoryToString(r.category)) +
                        ": " + (r.name.empty() ? r.url : r.name);
    options.push_back(std::move(label));
  }
  if (options.empty())
    options.push_back("(No text resources loaded)");

  if (s.selected_source_index >= static_cast<int>(s.source_resources.size()))
    s.selected_source_index = 0;

  s.source_combo_box->SetOptions(options);
  s.source_combo_box->SetSelection(s.selected_source_index);

  if (!s.source_resources.empty()) {
    const auto& selected = s.source_resources[s.selected_source_index];
    std::string text = ReadResourceText(selected);
    if (text.empty())
      text = "// No source data available for " + selected.url;
    s.source_viewer->SetContent(
        text, selected.name.empty() ? "source.html" : selected.name);
  } else {
    s.source_viewer->SetContent("// No page loaded in the active tab.",
                                "source.txt");
  }
}

// Rebuilds the Resources panel table and summary.
void RefreshResourcesPanel() {
  auto& s = State();
  if (!s.resources_data_source || !s.resources_table)
    return;

  s.all_page_resources.clear();
  Window* gw = GetActiveTab();
  if (gw && gw->GetBrowserWindow())
    s.all_page_resources = CollectPageResources(gw->GetBrowserWindow());

  std::vector<PageResource> filtered;
  size_t total_bytes = 0;
  size_t doc_count = 0;
  size_t css_count = 0;
  size_t js_count = 0;
  size_t img_count = 0;

  for (const auto& r : s.all_page_resources) {
    if (!s.resources_filter.empty() &&
        !ContainsIgnoreCase(r.name, s.resources_filter) &&
        !ContainsIgnoreCase(r.url, s.resources_filter) &&
        !ContainsIgnoreCase(r.mime_type, s.resources_filter) &&
        !ContainsIgnoreCase(ResourceCategoryToString(r.category),
                            s.resources_filter))
      continue;
    total_bytes += r.size_bytes;
    switch (r.category) {
      case ResourceCategory::kDocument:
      case ResourceCategory::kFrame:
        doc_count++;
        break;
      case ResourceCategory::kStylesheet:
        css_count++;
        break;
      case ResourceCategory::kScript:
        js_count++;
        break;
      case ResourceCategory::kImage:
      case ResourceCategory::kBackgroundImage:
        img_count++;
        break;
      default:
        break;
    }
    filtered.push_back(r);
  }

  if (s.resources_summary_label) {
    std::string summary =
        std::to_string(filtered.size()) + " resources · " +
        FormatByteSize(total_bytes) + " (" + std::to_string(doc_count) +
        " Doc, " + std::to_string(css_count) + " CSS, " +
        std::to_string(js_count) + " JS, " + std::to_string(img_count) +
        " Img)";
    s.resources_summary_label->SetText(summary);
  }

  s.resources_data_source->SetResources(std::move(filtered));
  s.resources_table->Refresh();

  const auto& displayed = s.resources_data_source->GetResources();
  if (s.selected_resource_index < 0 && !displayed.empty())
    s.selected_resource_index = 0;
  else if (s.selected_resource_index >= static_cast<int>(displayed.size()))
    s.selected_resource_index = static_cast<int>(displayed.size()) - 1;

  UpdateSelectedResourcePreview();
}

// Categorizes a NetworkLogEntry into one of the filter buckets (1=Doc, 2=CSS, 3=JS, 4=Img, 5=Other).
int CategorizeNetworkEntry(const NetworkLogEntry& e) {
  std::string mime = net_compat::GetMimeType(e);
  if (ContainsIgnoreCase(mime, "html") || ContainsIgnoreCase(mime, "xhtml"))
    return 1;
  if (ContainsIgnoreCase(mime, "css") || ContainsIgnoreCase(e.url, ".css"))
    return 2;
  if (ContainsIgnoreCase(mime, "javascript") ||
      ContainsIgnoreCase(mime, "ecmascript") ||
      ContainsIgnoreCase(e.url, ".js"))
    return 3;
  if (ContainsIgnoreCase(mime, "image/") || ContainsIgnoreCase(e.url, ".png") ||
      ContainsIgnoreCase(e.url, ".jpg") || ContainsIgnoreCase(e.url, ".jpeg") ||
      ContainsIgnoreCase(e.url, ".gif") || ContainsIgnoreCase(e.url, ".svg") ||
      ContainsIgnoreCase(e.url, ".webp") || ContainsIgnoreCase(e.url, ".ico"))
    return 4;
  return 5;
}

// Rebuilds the Network panel table and footer summary.
void RefreshNetworkPanel() {
  auto& s = State();
  if (!s.network_data_source || !s.network_table)
    return;

  std::unordered_set<std::string> tab_urls;
  int64_t nav_start_ms = 0;
  Window* gw = GetActiveTab();
  if (gw && gw->GetBrowserWindow()) {
    auto nav_it = s.tab_nav_start_ms.find(gw);
    if (nav_it != s.tab_nav_start_ms.end())
      nav_start_ms = nav_it->second;
    auto page_res = CollectPageResources(gw->GetBrowserWindow());
    for (const auto& r : page_res) {
      if (!r.url.empty())
        tab_urls.insert(r.url);
    }
  }

  const auto& raw_entries = net_compat::FetchEntries();
  std::vector<NetworkLogEntry> filtered;
  size_t total_bytes = 0;
  int64_t min_start = 0;
  int64_t max_end = 0;

  for (const auto& e : raw_entries) {
    if (s.network_this_page_only && (!tab_urls.empty() || nav_start_ms > 0)) {
      std::string redir = net_compat::GetRedirectUrl(e);
      bool matches_url =
          tab_urls.count(e.url) > 0 ||
          (!redir.empty() && tab_urls.count(redir) > 0);
      bool matches_time =
          nav_start_ms > 0 &&
          static_cast<int64_t>(e.start_time_ms) >= nav_start_ms;
      if (!matches_url && !matches_time)
        continue;
    }

    if (s.network_type_filter > 0 &&
        CategorizeNetworkEntry(e) != s.network_type_filter)
      continue;

    if (!s.network_text_filter.empty() &&
        !ContainsIgnoreCase(e.url, s.network_text_filter) &&
        !ContainsIgnoreCase(e.method, s.network_text_filter) &&
        !ContainsIgnoreCase(net_compat::GetMimeType(e),
                            s.network_text_filter) &&
        !ContainsIgnoreCase(net_compat::GetStatusString(e),
                            s.network_text_filter))
      continue;

    total_bytes += e.bytes_received;
    int64_t st_ms = static_cast<int64_t>(e.start_time_ms);
    int64_t end_ms = static_cast<int64_t>(e.end_time_ms);
    if (st_ms > 0 && (min_start == 0 || st_ms < min_start))
      min_start = st_ms;
    if (end_ms > max_end)
      max_end = end_ms;

    filtered.push_back(e);
  }

  s.network_data_source->SetEntries(std::move(filtered));
  s.network_table->Refresh();

  if (s.network_footer_label) {
    int64_t elapsed = (max_end > min_start && min_start > 0)
                          ? (max_end - min_start)
                          : 0;
    std::string footer =
        std::to_string(s.network_data_source->GetNumberOfRows()) +
        " requests · " + FormatByteSize(total_bytes) +
        " transferred · Total time: " + std::to_string(elapsed) + " ms";
    s.network_footer_label->SetText(footer);
  }

  UpdateSelectedNetworkEntryDetails();
}

// Rebuilds the Document (DOM / Box Tree) dump in the Document panel.
void RefreshDocumentPanel() {
  auto& s = State();
  if (!s.document_viewer)
    return;

  Window* gw = GetActiveTab();
  if (!gw || !gw->GetBrowserWindow()) {
    s.document_viewer->SetContent("// No active document loaded.", "dom.txt");
    return;
  }

  char* buf = nullptr;
  size_t len = 0;
  {
    NETSURF_LOCK;
    FILE* stream = open_memstream(&buf, &len);
    if (stream != nullptr) {
      enum content_debug op = (s.document_tree_mode == 0)
                                  ? CONTENT_DEBUG_DOM
                                  : CONTENT_DEBUG_RENDER;
      browser_window_debug_dump(gw->GetBrowserWindow(), stream, op);
      fclose(stream);
    }
  }

  if (buf != nullptr) {
    std::string dump(buf, len);
    free(buf);
    if (dump.empty())
      dump = "// Document tree dump is empty.";
    s.document_viewer->SetContent(
        dump, s.document_tree_mode == 0 ? "dom-tree.txt" : "box-tree.txt");
  } else {
    s.document_viewer->SetContent("// Failed to capture document debug dump.",
                                  "dom.txt");
  }
}

// Rebuilds the Page Info panel fields for the active browser tab.
void RefreshPageInfoPanel() {
  auto& s = State();
  if (!s.pi_title_val)
    return;

  Window* gw = GetActiveTab();
  if (!gw || !gw->GetBrowserWindow()) {
    s.page_info_host.clear();
    s.pi_title_val->SetText("-");
    s.pi_url_val->SetText("-");
    s.pi_host_val->SetText("-");
    s.pi_mime_val->SetText("-");
    s.pi_encoding_val->SetText("-");
    s.pi_quirks_val->SetText("-");
    s.pi_js_val->SetText("-");
    s.pi_zoom_val->SetText("-");
    s.pi_extents_val->SetText("-");
    s.pi_security_val->SetText("-");
    s.pi_mixed_val->SetText("-");
    s.pi_cookies_val->SetText("-");
    return;
  }

  NETSURF_LOCK;
  struct browser_window* bw = gw->GetBrowserWindow();
  const char* title = browser_window_get_title(bw);
  s.pi_title_val->SetText(title && title[0] ? title : "(Untitled)");

  struct nsurl* url = nullptr;
  std::string url_str = "-";
  std::string host_str;
  if (browser_window_get_url(bw, true, &url) == NSERROR_OK && url != nullptr) {
    if (const char* raw_url = nsurl_access(url))
      url_str = raw_url;
    lwc_string* host_lwc = nsurl_get_component(url, NSURL_HOST);
    if (host_lwc != nullptr) {
      host_str.assign(lwc_string_data(host_lwc), lwc_string_length(host_lwc));
      lwc_string_unref(host_lwc);
    }
    nsurl_unref(url);
  }
  s.page_info_host = host_str;
  s.pi_url_val->SetText(url_str);
  s.pi_host_val->SetText(host_str.empty() ? "(Local / Internal)" : host_str);

  struct hlcache_handle* content = browser_window_get_content(bw);
  if (content != nullptr) {
    lwc_string* mime_lwc = content_get_mime_type(content);
    if (mime_lwc != nullptr) {
      s.pi_mime_val->SetText(std::string_view(lwc_string_data(mime_lwc),
                                              lwc_string_length(mime_lwc)));
      lwc_string_unref(mime_lwc);
    } else {
      s.pi_mime_val->SetText("-");
    }

    const char* enc = content_get_encoding(content, CONTENT_ENCODING_NORMAL);
    const char* enc_src =
        content_get_encoding(content, CONTENT_ENCODING_SOURCE);
    std::string enc_text = (enc && enc[0]) ? enc : "UTF-8 (Default)";
    if (enc_src && enc_src[0])
      enc_text += std::string(" (") + enc_src + ")";
    s.pi_encoding_val->SetText(enc_text);

    bool quirky = content_get_quirks(content);
    s.pi_quirks_val->SetText(quirky ? "Quirks Mode" : "Standards Mode");

    bool mixed = content_saw_insecure_objects(content);
    s.pi_mixed_val->SetText(
        mixed ? "Yes (Insecure subresources loaded)" : "No");
  } else {
    s.pi_mime_val->SetText("-");
    s.pi_encoding_val->SetText("-");
    s.pi_quirks_val->SetText("-");
    s.pi_mixed_val->SetText("-");
  }

  s.pi_js_val->SetText(nsoption_bool(enable_javascript) ? "Enabled"
                                                        : "Disabled");

  int zoom_pct =
      static_cast<int>(std::round(browser_window_get_scale(bw) * 100.0f));
  s.pi_zoom_val->SetText(std::to_string(zoom_pct) + "%");

  int ext_w = 0;
  int ext_h = 0;
  if (browser_window_get_extents(bw, true, &ext_w, &ext_h) == NSERROR_OK) {
    s.pi_extents_val->SetText(std::to_string(ext_w) + " × " +
                              std::to_string(ext_h) + " px");
  } else {
    s.pi_extents_val->SetText("-");
  }

  browser_window_page_info_state state = browser_window_get_page_info_state(bw);
  std::string_view sec_str = "Unknown";
  switch (state) {
    case PAGE_STATE_INTERNAL:
      sec_str = "Internal NetSurf Page";
      break;
    case PAGE_STATE_LOCAL:
      sec_str = "Local File Resource";
      break;
    case PAGE_STATE_INSECURE:
      sec_str = "Not Secure (HTTP)";
      break;
    case PAGE_STATE_SECURE_OVERRIDE:
      sec_str = "Secure (Certificate Override Active)";
      break;
    case PAGE_STATE_SECURE_ISSUES:
      sec_str = "Partially Secure (Mixed Content)";
      break;
    case PAGE_STATE_SECURE:
      sec_str = "Secure (Verified HTTPS)";
      break;
    default:
      break;
  }
  s.pi_security_val->SetText(sec_str);

  int cookie_count = browser_window_get_cookie_count(bw);
  s.pi_cookies_val->SetText(std::to_string(cookie_count) + " cookie(s)");
}

// Refreshes whichever Developer Tools panel is currently active.
void RefreshActivePanel() {
  auto& s = State();
  if (!s.is_open)
    return;
  switch (s.active_panel) {
    case DevToolsPanel::Console:
      RefreshConsolePanel();
      break;
    case DevToolsPanel::Source:
      RefreshSourcePanel();
      break;
    case DevToolsPanel::Resources:
      RefreshResourcesPanel();
      break;
    case DevToolsPanel::Network:
      RefreshNetworkPanel();
      break;
    case DevToolsPanel::Document:
      RefreshDocumentPanel();
      break;
    case DevToolsPanel::PageInfo:
      RefreshPageInfoPanel();
      break;
  }
}

// Synchronizes the Developer Tools root node between docked container and pop-out UiWindow.
void SyncDockPlacement() {
  EnsureDevToolsInitialized();
  auto& s = State();

  if (s.dock_toggle_image_button) {
    auto target_img = s.is_docked ? s.popout_icon : s.dock_icon;
    if (auto iv = s.dock_toggle_image_button->GetImageView().lock())
      iv->SetImage(target_img);
  }
  if (s.dock_toggle_node)
    Tooltip::Attach(s.dock_toggle_node,
                    s.is_docked ? "Undock into Separate Window"
                                : "Dock to Bottom");
  if (s.close_button_node) {
    YGDisplay desired = s.is_docked ? YGDisplayFlex : YGDisplayNone;
    if (s.close_button_node->GetLayout().GetDisplay() != desired)
      s.close_button_node->GetLayout().SetDisplay(desired);
  }
  if (s.root_node) {
    if (auto block = s.root_node->Get<Block>())
      block->SetBorderWidth(s.is_docked ? 1.0f : 0.0f);
    s.root_node->GetLayout().SetPadding(
        YGEdgeAll, s.is_docked ? ::perception::ui::kContainerPadding : 0.0f);
  }

  if (!s.is_open) {
    if (s.popup_window_node) {
      s.is_closing_popup_internally = true;
      s.popup_window_node->RemoveChild(s.root_node);
      if (auto uw = s.popup_window_node->Get<UiWindow>())
        uw->Close();
      s.popup_window_node.reset();
      s.is_closing_popup_internally = false;
    }
    if (s.docked_host_node) {
      s.docked_host_node->RemoveChildren();
      s.docked_host_node->GetLayout().SetDisplay(YGDisplayNone);
    }
    if (s.layout_callback)
      s.layout_callback();
    if (auto win = GetGlobalUiWindow())
      win->Invalidate();
    return;
  }

  if (s.is_docked) {
    if (s.popup_window_node) {
      s.is_closing_popup_internally = true;
      s.popup_window_node->RemoveChild(s.root_node);
      if (auto uw = s.popup_window_node->Get<UiWindow>())
        uw->Close();
      s.popup_window_node.reset();
      s.is_closing_popup_internally = false;
    }
    if (s.docked_host_node) {
      s.docked_host_node->RemoveChildren();
      s.docked_host_node->AddChild(s.root_node);
      s.docked_host_node->GetLayout().SetDisplay(YGDisplayFlex);
    }
  } else {
    if (s.docked_host_node) {
      s.docked_host_node->RemoveChildren();
      s.docked_host_node->GetLayout().SetDisplay(YGDisplayNone);
    }
    if (!s.popup_window_node) {
      s.popup_window_node = UiWindow::ResizableWindowWithTitleBar(
          "Developer Tools",
          [](UiWindow& window) {
            window.OnClose([]() {
              auto& st = State();
              if (st.is_closing_popup_internally)
                return;
              st.popup_window_node.reset();
              st.is_open = false;
              if (st.layout_callback)
                st.layout_callback();
            });
          },
          s.root_node);
    } else if (auto uw = s.popup_window_node->Get<UiWindow>()) {
      uw->Focus();
    }
  }

  if (s.layout_callback)
    s.layout_callback();
  if (auto win = GetGlobalUiWindow())
    win->Invalidate();
}

}  // namespace

void AppendConsoleMessage(Window* gw,
                          browser_window_console_source src,
                          const char* msg,
                          size_t msglen,
                          browser_window_console_flags flags) {
  if (!gw || !msg)
    return;

  ConsoleLevel level = ConsoleLevel::Log;
  switch (flags & BW_CS_FLAG_LEVEL_MASK) {
    case BW_CS_FLAG_LEVEL_DEBUG:
      level = ConsoleLevel::Debug;
      break;
    case BW_CS_FLAG_LEVEL_LOG:
      level = ConsoleLevel::Log;
      break;
    case BW_CS_FLAG_LEVEL_INFO:
      level = ConsoleLevel::Info;
      break;
    case BW_CS_FLAG_LEVEL_WARN:
      level = ConsoleLevel::Warn;
      break;
    case BW_CS_FLAG_LEVEL_ERROR:
      level = ConsoleLevel::Error;
      break;
    default:
      break;
  }
  if (src == BW_CS_SCRIPT_ERROR && level != ConsoleLevel::Warn)
    level = ConsoleLevel::Error;

  ConsoleMessage entry{
      .source = src,
      .level = level,
      .foldable = (flags & BW_CS_FLAG_FOLDABLE) != 0,
      .text = std::string(msg, msglen),
      .timestamp_ms = CurrentTimeMs(),
  };

  auto& s = State();
  auto& log = s.tab_logs[gw];
  if (log.size() >= kMaxConsoleMessagesPerTab)
    log.pop_front();
  log.push_back(std::move(entry));

  NotifyConsoleListeners();
  if (s.is_open && s.active_panel == DevToolsPanel::Console &&
      gw == GetActiveTab())
    RefreshConsolePanel();
}

void ClearConsoleMessagesForTab(Window* gw, bool force) {
  if (!gw)
    return;
  auto& s = State();
  if (!force && s.preserve_console_log)
    return;

  auto it = s.tab_logs.find(gw);
  if (it != s.tab_logs.end())
    it->second.clear();

  NotifyConsoleListeners();
  if (s.is_open && s.active_panel == DevToolsPanel::Console &&
      gw == GetActiveTab())
    RefreshConsolePanel();
}

ConsoleCounts GetConsoleCountsForTab(Window* gw) {
  ConsoleCounts counts{};
  if (!gw)
    return counts;
  auto& s = State();
  auto it = s.tab_logs.find(gw);
  if (it == s.tab_logs.end())
    return counts;
  for (const auto& msg : it->second) {
    if (msg.level == ConsoleLevel::Error)
      counts.error_count++;
    else if (msg.level == ConsoleLevel::Warn)
      counts.warning_count++;
  }
  return counts;
}

uint64_t AddConsoleListener(std::function<void()> on_changed) {
  auto& s = State();
  uint64_t id = s.next_console_listener_id++;
  s.console_listeners[id] = std::move(on_changed);
  return id;
}

void RemoveConsoleListener(uint64_t listener_id) {
  State().console_listeners.erase(listener_id);
}

void OpenDevTools(DevToolsPanel panel) {
  EnsureDevToolsInitialized();
  auto& s = State();
  s.active_panel = panel;
  s.is_open = true;
  if (s.main_tab_bar)
    s.main_tab_bar->SetSelectedIndex(static_cast<int>(panel), false);
  for (int i = 0; i < 6; ++i) {
    if (s.panel_nodes[i])
      s.panel_nodes[i]->GetLayout().SetDisplay(
          i == static_cast<int>(panel) ? YGDisplayFlex : YGDisplayNone);
  }
  SyncDockPlacement();
  RefreshActivePanel();
}

void ToggleDevTools(DevToolsPanel panel) {
  auto& s = State();
  if (s.is_open && s.active_panel == panel) {
    CloseDevTools();
  } else {
    OpenDevTools(panel);
  }
}

void CloseDevTools() {
  auto& s = State();
  if (!s.is_open)
    return;
  s.is_open = false;
  SyncDockPlacement();
}

bool IsDevToolsOpen() { return State().is_open; }

bool IsDevToolsDocked() { return State().is_docked; }

void SetDevToolsDocked(bool docked) {
  auto& s = State();
  if (s.is_docked == docked)
    return;
  s.is_docked = docked;
  if (s.is_open)
    SyncDockPlacement();
}

std::shared_ptr<Node> GetDockedDevToolsNode() {
  EnsureDevToolsInitialized();
  return State().docked_host_node;
}

void SetDevToolsLayoutCallback(std::function<void()> callback) {
  State().layout_callback = std::move(callback);
}

void NotifyDevToolsTabChanged() {
  auto& s = State();
  s.selected_source_index = 0;
  s.selected_resource_index = 0;
  if (s.is_open)
    RefreshActivePanel();
}

void NotifyDevToolsPageLoaded(Window* gw) {
  if (gw == GetActiveTab() && State().is_open)
    RefreshActivePanel();
}

void NotifyDevToolsNavigationStarted(Window* gw) {
  if (!gw)
    return;
  auto& s = State();
  s.tab_nav_start_ms[gw] = CurrentTimeMs();
  if (!s.preserve_console_log)
    ClearConsoleMessagesForTab(gw, false);
  if (gw == GetActiveTab() && !s.preserve_network_log &&
      !s.network_this_page_only)
    ClearNetworkLog();
  if (gw == GetActiveTab() && s.is_open)
    RefreshActivePanel();
}

void InspectResourceUrl(std::string_view url) {
  OpenDevTools(DevToolsPanel::Resources);
  auto& s = State();
  RefreshResourcesPanel();
  if (!s.resources_data_source)
    return;
  const auto& list = s.resources_data_source->GetResources();
  for (size_t i = 0; i < list.size(); ++i) {
    if (list[i].url == url) {
      s.selected_resource_index = static_cast<int>(i);
      UpdateSelectedResourcePreview();
      return;
    }
  }
}

}  // namespace perception
}  // namespace netsurf
