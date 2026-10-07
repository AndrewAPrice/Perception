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

#include "download.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern "C" {
#include "utils/errors.h"
#include "desktop/download.h"
#include "netsurf/download.h"
#include "utils/log.h"
}

#include "perception/scheduler.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/file_dialog.h"
#include "perception/ui/components/image_button.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/table.h"
#include "perception/ui/components/tooltip.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/image.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/size.h"
#include "tabs.h"
#include "window.h"

namespace {

// Default directory where downloads are saved.
constexpr char kDefaultDownloadDirectory[] = "/tmp/Downloads";

// Fallback filename when the server does not provide one.
constexpr char kFallbackDownloadFilename[] = "download";

// Path to the close (cancel) icon asset.
constexpr std::string_view kCloseIconPath = "/Applications/NetSurf/close.svg";

// Path to the clear-all icon asset.
constexpr std::string_view kClearAllIconPath =
    "/Applications/NetSurf/clear-all.svg";

// Width of the Downloads manager window in pixels.
constexpr float kDownloadsWindowWidth = 720.0f;

// Height of the Downloads manager window in pixels.
constexpr float kDownloadsWindowHeight = 400.0f;

// Width of the Filename column in the Downloads table.
constexpr float kFilenameColumnWidth = 190.0f;

// Width of the Status column in the Downloads table.
constexpr float kStatusColumnWidth = 110.0f;

// Width of the Progress column in the Downloads table.
constexpr float kProgressColumnWidth = 180.0f;

// Number of bytes in one kilobyte.
constexpr uint64_t kBytesPerKilobyte = 1024;

// Number of bytes in one megabyte.
constexpr uint64_t kBytesPerMegabyte = 1024 * 1024;

enum class DownloadState {
  kDownloading,
  kComplete,
  kError,
  kCancelled,
};

}  // namespace

struct gui_download_window {
  download_context* ctx = nullptr;
  std::string filename;
  std::string destination_path;
  std::string error_message;
  uint64_t total_bytes = 0;
  uint64_t received_bytes = 0;
  DownloadState state = DownloadState::kDownloading;
  bool waiting_for_path = true;
  bool done_received = false;
  std::vector<char> pending_buffer;
  FILE* file = nullptr;
};

namespace netsurf {
namespace perception {
namespace {

using ::perception::ui::Image;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::CellHighlightability;
using ::perception::ui::components::Container;
using ::perception::ui::components::ImageButton;
using ::perception::ui::components::ShowSaveFileDialog;
using ::perception::ui::components::Table;
using ::perception::ui::components::Tooltip;
using ::perception::ui::components::UiWindow;

std::vector<std::shared_ptr<struct gui_download_window>> downloads;
std::shared_ptr<Node> downloads_window_node;
std::shared_ptr<UiWindow> downloads_ui_window;
std::shared_ptr<Table> downloads_table;
int selected_download_row = -1;

struct ListenerEntry {
  int id;
  std::function<void()> callback;
};

int next_listener_id = 1;
std::vector<ListenerEntry> download_listeners;

std::string FormatByteCount(uint64_t bytes) {
  std::ostringstream oss;
  if (bytes >= kBytesPerMegabyte) {
    oss << std::fixed << std::setprecision(1)
        << (static_cast<double>(bytes) / static_cast<double>(kBytesPerMegabyte))
        << " MB";
  } else if (bytes >= kBytesPerKilobyte) {
    oss << std::fixed << std::setprecision(1)
        << (static_cast<double>(bytes) / static_cast<double>(kBytesPerKilobyte))
        << " KB";
  } else {
    oss << bytes << " B";
  }
  return oss.str();
}

std::string FormatStatus(const gui_download_window& item) {
  switch (item.state) {
    case DownloadState::kDownloading:
      return item.waiting_for_path ? "Choosing path..." : "Downloading";
    case DownloadState::kComplete:
      return "Complete";
    case DownloadState::kCancelled:
      return "Cancelled";
    case DownloadState::kError:
      if (!item.error_message.empty())
        return "Error: " + item.error_message;
      return "Error";
  }
  return "Unknown";
}

std::string FormatProgress(const gui_download_window& item) {
  if (item.total_bytes > 0) {
    uint64_t pct = (item.received_bytes * 100) / item.total_bytes;
    return FormatByteCount(item.received_bytes) + " / " +
           FormatByteCount(item.total_bytes) + " (" + std::to_string(pct) +
           "%)";
  }
  return FormatByteCount(item.received_bytes);
}

void NotifyDownloadUpdate() {
  if (downloads_table)
    downloads_table->Refresh();
  auto listeners_copy = download_listeners;
  for (const auto& entry : listeners_copy) {
    if (entry.callback)
      entry.callback();
  }
}

class DownloadsTableDataSource : public Table::DataSource {
 public:
  int GetNumberOfRows() override { return static_cast<int>(downloads.size()); }

  std::string GetCellValue(int row_index, int column_index) override {
    if (row_index < 0 || row_index >= static_cast<int>(downloads.size()))
      return "";
    const auto& item = *downloads[row_index];
    switch (column_index) {
      case 0:
        return item.filename;
      case 1:
        return FormatStatus(item);
      case 2:
        return FormatProgress(item);
      case 3:
        return item.destination_path;
      default:
        return "";
    }
  }

  void SortByColumn(int, bool) override {}

  CellHighlightability GetCellHighlightablity(int, int) override {
    return CellHighlightability::RowHighlightable;
  }
};

std::shared_ptr<DownloadsTableDataSource> downloads_data_source;

void CancelDownloadItem(const std::shared_ptr<gui_download_window>& item) {
  if (!item || item->state != DownloadState::kDownloading)
    return;
  item->state = DownloadState::kCancelled;
  item->pending_buffer.clear();
  if (item->file != nullptr) {
    std::fclose(item->file);
    item->file = nullptr;
  }
  if (item->ctx != nullptr) {
    NETSURF_LOCK;
    if (item->ctx != nullptr) {
      download_context* ctx = item->ctx;
      item->ctx = nullptr;
      download_context_abort(ctx);
      download_context_destroy(ctx);
    }
  }
  NotifyDownloadUpdate();
}

struct gui_download_window* DownloadCreate(struct download_context* ctx,
                                           struct gui_window* /*parent*/) {
  std::error_code ec;
  std::filesystem::create_directories(kDefaultDownloadDirectory, ec);

  auto item = std::make_shared<struct gui_download_window>();
  item->ctx = ctx;
  const char* raw_name = download_context_get_filename(ctx);
  item->filename =
      (raw_name != nullptr && raw_name[0] != '\0') ? raw_name
                                                   : kFallbackDownloadFilename;
  item->total_bytes = download_context_get_total_length(ctx);
  item->received_bytes = 0;
  item->state = DownloadState::kDownloading;
  item->waiting_for_path = true;

  downloads.push_back(item);
  NotifyDownloadUpdate();

  std::string default_path =
      std::string(kDefaultDownloadDirectory) + "/" + item->filename;

  ShowSaveFileDialog(
      [item](bool succeeded, std::string_view chosen_path) {
        item->waiting_for_path = false;
        if (!succeeded || chosen_path.empty()) {
          CancelDownloadItem(item);
          return;
        }

        item->destination_path = std::string(chosen_path);
        std::filesystem::path p(item->destination_path);
        if (p.has_filename())
          item->filename = p.filename().string();
        if (p.has_parent_path()) {
          std::error_code dir_ec;
          std::filesystem::create_directories(p.parent_path(), dir_ec);
        }

        if (item->state == DownloadState::kCancelled ||
            item->state == DownloadState::kError) {
          item->pending_buffer.clear();
          NotifyDownloadUpdate();
          return;
        }

        item->file = std::fopen(item->destination_path.c_str(), "wb");
        if (item->file == nullptr) {
          item->state = DownloadState::kError;
          item->error_message = "Failed to open destination file";
          item->pending_buffer.clear();
          if (item->ctx != nullptr) {
            NETSURF_LOCK;
            if (item->ctx != nullptr) {
              download_context* c = item->ctx;
              item->ctx = nullptr;
              download_context_abort(c);
              download_context_destroy(c);
            }
          }
          NotifyDownloadUpdate();
          return;
        }

        if (!item->pending_buffer.empty()) {
          size_t written = std::fwrite(item->pending_buffer.data(), 1,
                                       item->pending_buffer.size(), item->file);
          if (written != item->pending_buffer.size()) {
            item->state = DownloadState::kError;
            item->error_message = "Disk write failed";
          }
          item->pending_buffer.clear();
        }

        if (item->done_received) {
          std::fflush(item->file);
          std::fclose(item->file);
          item->file = nullptr;
          if (item->state == DownloadState::kDownloading)
            item->state = DownloadState::kComplete;
        }

        NotifyDownloadUpdate();
      },
      {}, default_path, kDefaultDownloadDirectory, "Save Download",
      GetGlobalUiWindow());

  return item.get();
}

nserror DownloadData(struct gui_download_window* dw, const char* data,
                     unsigned int size) {
  if (dw == nullptr)
    return NSERROR_INVALID;
  if (dw->state == DownloadState::kCancelled ||
      dw->state == DownloadState::kError)
    return NSERROR_SAVE_FAILED;

  dw->received_bytes += size;

  if (dw->waiting_for_path) {
    dw->pending_buffer.insert(dw->pending_buffer.end(), data, data + size);
  } else if (dw->file != nullptr) {
    size_t written = std::fwrite(data, 1, size, dw->file);
    if (written != size) {
      dw->state = DownloadState::kError;
      dw->error_message = "Disk write failed";
      std::fclose(dw->file);
      dw->file = nullptr;
      NotifyDownloadUpdate();
      return NSERROR_SAVE_FAILED;
    }
  }

  NotifyDownloadUpdate();
  return NSERROR_OK;
}

void DownloadError(struct gui_download_window* dw, const char* error_msg) {
  if (dw == nullptr)
    return;
  dw->state = DownloadState::kError;
  if (error_msg != nullptr)
    dw->error_message = error_msg;
  dw->pending_buffer.clear();
  if (dw->file != nullptr) {
    std::fclose(dw->file);
    dw->file = nullptr;
  }
  NotifyDownloadUpdate();
}

void DownloadDone(struct gui_download_window* dw) {
  if (dw == nullptr)
    return;
  dw->done_received = true;

  if (!dw->waiting_for_path) {
    if (dw->file != nullptr) {
      std::fflush(dw->file);
      std::fclose(dw->file);
      dw->file = nullptr;
    }
    if (dw->state == DownloadState::kDownloading)
      dw->state = DownloadState::kComplete;
  }

  if (dw->ctx != nullptr) {
    download_context* ctx = dw->ctx;
    dw->ctx = nullptr;
    download_context_destroy(ctx);
  }

  NotifyDownloadUpdate();
}

}  // namespace

struct gui_download_table perception_download_table = {
    .create = DownloadCreate,
    .data = DownloadData,
    .error = DownloadError,
    .done = DownloadDone,
};

void ShowDownloadsWindow() {
  if (downloads_ui_window) {
    downloads_ui_window->Focus();
    return;
  }

  if (!downloads_data_source)
    downloads_data_source = std::make_shared<DownloadsTableDataSource>();

  std::vector<Table::Column> columns = {
      {.title = "File",
       .layout_modifier = [](Layout& l) { l.SetWidth(kFilenameColumnWidth); },
       .sortable = false},
      {.title = "Status",
       .layout_modifier = [](Layout& l) { l.SetWidth(kStatusColumnWidth); },
       .sortable = false},
      {.title = "Progress",
       .layout_modifier = [](Layout& l) { l.SetWidth(kProgressColumnWidth); },
       .sortable = false},
      {.title = "Destination",
       .layout_modifier = [](Layout& l) { l.SetFlexGrow(1.0f); },
       .sortable = false},
  };

  auto table_node = Table::BasicTable(
      downloads_data_source, columns, &downloads_table,
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetWidthPercent(100.0f);
      },
      [](Table& table) {
        table.OnCellSelect([](int row, int) { selected_download_row = row; });
      });

  auto close_icon = Image::LoadImage(kCloseIconPath);
  auto clear_all_icon = Image::LoadImage(kClearAllIconPath);

  auto toolbar = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetFlexShrink(0.0f);
      },
      ImageButton::BasicImageButton(
          []() {
            if (selected_download_row < 0 ||
                selected_download_row >= static_cast<int>(downloads.size()))
              return;
            CancelDownloadItem(downloads[selected_download_row]);
          },
          close_icon, Tooltip::ShowTooltip("Cancel Selected Download")),
      ImageButton::BasicImageButton(
          []() {
            downloads.erase(
                std::remove_if(
                    downloads.begin(), downloads.end(),
                    [](const std::shared_ptr<gui_download_window>& item) {
                      return item->state != DownloadState::kDownloading;
                    }),
                downloads.end());
            selected_download_row = -1;
            NotifyDownloadUpdate();
          },
          clear_all_icon, Tooltip::ShowTooltip("Clear Finished Downloads")));

  downloads_window_node = UiWindow::ResizableWindowWithTitleBar(
      "Downloads", &downloads_ui_window,
      [](Layout& layout) {
        layout.SetWidth(kDownloadsWindowWidth);
        layout.SetHeight(kDownloadsWindowHeight);
      },
      [](UiWindow& window) {
        window.OnClose([]() {
          downloads_table.reset();
          downloads_ui_window.reset();
          auto old_node = std::move(downloads_window_node);
          ::perception::Defer([old_node]() {});
        });
      },
      toolbar, table_node);
}

DownloadSummary GetDownloadSummary() {
  DownloadSummary summary;
  summary.total_count = static_cast<int>(downloads.size());
  for (const auto& item : downloads) {
    if (item->state == DownloadState::kDownloading) {
      summary.active_count++;
      summary.active_received_bytes += item->received_bytes;
      summary.active_total_bytes += item->total_bytes;
    }
  }
  if (summary.active_total_bytes > 0)
    summary.average_percent = static_cast<int>(
        (summary.active_received_bytes * 100) / summary.active_total_bytes);
  return summary;
}

int AddDownloadListener(std::function<void()> on_changed) {
  int id = next_listener_id++;
  download_listeners.push_back({id, std::move(on_changed)});
  return id;
}

void RemoveDownloadListener(int listener_id) {
  download_listeners.erase(
      std::remove_if(download_listeners.begin(), download_listeners.end(),
                     [listener_id](const ListenerEntry& entry) {
                       return entry.id == listener_id;
                     }),
      download_listeners.end());
}

}  // namespace perception
}  // namespace netsurf
