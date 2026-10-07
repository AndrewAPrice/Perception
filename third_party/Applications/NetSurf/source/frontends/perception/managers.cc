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

#include "managers.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

extern "C" {
#include "utils/errors.h"
#include "content/urldb.h"
#include "desktop/cookie_manager.h"
#include "desktop/global_history.h"
#include "desktop/hotlist.h"
#include "desktop/local_history.h"
#include "desktop/page-info.h"
#include "netsurf/browser_window.h"
#include "netsurf/cookie_db.h"
#include "netsurf/keypress.h"
#include "netsurf/mouse.h"
#include "netsurf/types.h"
#include "netsurf/url_db.h"
#include "utils/log.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"

struct treeview;
struct treeview_node;
struct treeview_field_desc {
  void* field;
  int flags;
};
struct treeview_field_data {
  void* field;
  const char* value;
  size_t value_len;
};
nserror treeview_init(void);
nserror treeview_fini(void);
nserror treeview_set_search_string(struct treeview* tree, const char* string);

enum hotlist_fields {
  HL_TITLE,
  HL_URL,
  HL_LAST_VISIT,
  HL_VISITS,
  HL_FOLDER,
  HL_N_FIELDS
};
struct hotlist_folder;
struct hotlist_ctx {
  struct treeview* tree;
  struct treeview_field_desc fields[HL_N_FIELDS];
  bool built;
  struct hotlist_folder* default_folder;
  char* save_path;
  bool save_scheduled;
};
extern struct hotlist_ctx hl_ctx;

enum global_history_folders {
  GH_TODAY = 0,
  GH_YESTERDAY,
  GH_2_DAYS_AGO,
  GH_3_DAYS_AGO,
  GH_4_DAYS_AGO,
  GH_5_DAYS_AGO,
  GH_6_DAYS_AGO,
  GH_LAST_WEEK,
  GH_2_WEEKS_AGO,
  GH_3_WEEKS_AGO,
  GH_N_FOLDERS
};
enum global_history_fields {
  GH_TITLE,
  GH_URL,
  GH_LAST_VISIT,
  GH_VISITS,
  GH_PERIOD,
  N_FIELDS
};
struct global_history_folder {
  struct treeview_node* folder;
  struct treeview_field_data data;
};
struct global_history_ctx {
  struct treeview* tree;
  struct treeview_field_desc fields[N_FIELDS];
  struct global_history_folder folders[GH_N_FOLDERS];
  time_t today;
  int weekday;
  bool built;
};
extern struct global_history_ctx gh_ctx;
}

#include "core_window.h"
#include "perception/scheduler.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/image_button.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/pop_up.h"
#include "perception/ui/components/tooltip.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/image.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/ui/size.h"
#include "perception/ui/theme.h"
#include "settings.h"
#include "tabs.h"
#include "window.h"

namespace {

// Directory path used for persistent browser storage.
constexpr char kStorageDirectory[] = "/tmp/.netsurf";

// File path for storing bookmarks (hotlist).
constexpr char kHotlistFilePath[] = "/tmp/.netsurf/Hotlist";

// File path for storing visited URLs.
constexpr char kUrlsFilePath[] = "/tmp/.netsurf/URLs";

// File path for storing cookies.
constexpr char kCookiesFilePath[] = "/tmp/.netsurf/Cookies";

// Path to the add-bookmark icon asset.
constexpr std::string_view kBookmarkPlusIconPath =
    "/Applications/NetSurf/bookmark-plus.svg";

// Path to the new-folder icon asset.
constexpr std::string_view kFolderPlusIconPath =
    "/Applications/NetSurf/folder-plus.svg";

// Path to the edit icon asset.
constexpr std::string_view kEditIconPath = "/Applications/NetSurf/edit.svg";

// Path to the trash (delete) icon asset.
constexpr std::string_view kTrashIconPath = "/Applications/NetSurf/trash.svg";

// Path to the clear-all icon asset.
constexpr std::string_view kClearAllIconPath =
    "/Applications/NetSurf/clear-all.svg";

// Path to the expand-all icon asset.
constexpr std::string_view kExpandAllIconPath =
    "/Applications/NetSurf/expand-all.svg";

// Path to the collapse-all icon asset.
constexpr std::string_view kCollapseAllIconPath =
    "/Applications/NetSurf/collapse-all.svg";

// Default width of the Bookmarks manager window in pixels.
constexpr float kBookmarksWindowWidth = 700.0f;

// Default height of the Bookmarks manager window in pixels.
constexpr float kBookmarksWindowHeight = 500.0f;

// Default width of the Global History manager window in pixels.
constexpr float kHistoryWindowWidth = 700.0f;

// Default height of the Global History manager window in pixels.
constexpr float kHistoryWindowHeight = 500.0f;

// Default width of the Cookie Manager window in pixels.
constexpr float kCookieWindowWidth = 700.0f;

// Default height of the Cookie Manager window in pixels.
constexpr float kCookieWindowHeight = 500.0f;

// Default width of the Add/Edit Bookmark dialog in pixels.
constexpr float kBookmarkDialogWidth = 380.0f;

// Default height of the Add/Edit Bookmark dialog in pixels.
constexpr float kBookmarkDialogHeight = 160.0f;

// Fallback width for the local history popup when no size is reported.
constexpr float kLocalHistoryFallbackWidth = 300.0f;

// Fallback height for the local history popup when no size is reported.
constexpr float kLocalHistoryFallbackHeight = 250.0f;

// Fallback width for the page info popup when no size is reported.
constexpr float kPageInfoFallbackWidth = 340.0f;

// Fallback height for the page info popup when no size is reported.
constexpr float kPageInfoFallbackHeight = 220.0f;

// Maximum popup width in pixels to keep popups reasonably bounded.
constexpr float kMaxPopupWidth = 600.0f;

// Maximum popup height in pixels to keep popups reasonably bounded.
constexpr float kMaxPopupHeight = 500.0f;

// Border radius for popup containers.
constexpr float kPopupBorderRadius = 6.0f;

// Border width for popup containers.
constexpr float kPopupBorderWidth = 1.0f;

}  // namespace

namespace netsurf {
namespace perception {
namespace {

using ::perception::ui::Image;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::Size;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::ImageButton;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::PopUp;
using ::perception::ui::components::Tooltip;
using ::perception::ui::components::UiWindow;

bool managers_initialized = false;

std::shared_ptr<CoreWindowHost> bookmarks_host;
std::shared_ptr<Node> bookmarks_window_node;
std::shared_ptr<UiWindow> bookmarks_ui_window;
std::shared_ptr<InputBox> bookmarks_search_box;

std::shared_ptr<CoreWindowHost> history_host;
std::shared_ptr<Node> history_window_node;
std::shared_ptr<UiWindow> history_ui_window;
std::shared_ptr<InputBox> history_search_box;

std::shared_ptr<CoreWindowHost> cookies_host;
std::shared_ptr<Node> cookies_window_node;
std::shared_ptr<UiWindow> cookies_ui_window;
std::shared_ptr<InputBox> cookies_search_box;

void DetachHostRootNode(const std::shared_ptr<CoreWindowHost>& host) {
  if (!host || !host->GetRootNode())
    return;
  if (auto parent = host->GetRootNode()->GetParent().lock())
    parent->RemoveChild(host->GetRootNode());
}

}  // namespace

void InitializeManagers() {
  if (managers_initialized)
    return;

  bool persist = IsStoragePersistenceEnabled();
  bool storage_dir_ready = false;
  if (persist) {
    std::error_code ec;
    std::filesystem::create_directories(kStorageDirectory, ec);
    storage_dir_ready =
        !ec && std::filesystem::exists(kStorageDirectory, ec);
    if (storage_dir_ready) {
      nsoption_set_charp(cookie_file, strdup(kCookiesFilePath));
      nsoption_set_charp(cookie_jar, strdup(kCookiesFilePath));
      if (std::filesystem::exists(kUrlsFilePath, ec))
        urldb_load(kUrlsFilePath);
      if (std::filesystem::exists(kCookiesFilePath, ec))
        urldb_load_cookies(kCookiesFilePath);
    }
  }

  bookmarks_host = CoreWindowHost::Create(
      [](const struct rect& clip, const struct redraw_context& ctx) {
        hotlist_redraw(0, 0, const_cast<struct rect*>(&clip), &ctx);
      },
      [](browser_mouse_state mouse, int x, int y) {
        hotlist_mouse_action(mouse, x, y);
      },
      [](uint32_t nskey) -> bool { return hotlist_keypress(nskey); });

  history_host = CoreWindowHost::Create(
      [](const struct rect& clip, const struct redraw_context& ctx) {
        global_history_redraw(0, 0, const_cast<struct rect*>(&clip), &ctx);
      },
      [](browser_mouse_state mouse, int x, int y) {
        global_history_mouse_action(mouse, x, y);
      },
      [](uint32_t nskey) -> bool { return global_history_keypress(nskey); });

  cookies_host = CoreWindowHost::Create(
      [](const struct rect& clip, const struct redraw_context& ctx) {
        cookie_manager_redraw(0, 0, const_cast<struct rect*>(&clip), &ctx);
      },
      [](browser_mouse_state mouse, int x, int y) {
        cookie_manager_mouse_action(mouse, x, y);
      },
      [](uint32_t nskey) -> bool { return cookie_manager_keypress(nskey); });

  treeview_init();
  std::error_code ec;
  const char* hotlist_load =
      (storage_dir_ready && std::filesystem::exists(kHotlistFilePath, ec))
          ? kHotlistFilePath
          : nullptr;
  const char* hotlist_save = storage_dir_ready ? kHotlistFilePath : nullptr;
  hotlist_init(hotlist_load, hotlist_save);
  hotlist_manager_init(bookmarks_host->GetCoreWindow());
  global_history_init(history_host->GetCoreWindow());
  cookie_manager_init(cookies_host->GetCoreWindow());

  managers_initialized = true;
}

void SavePersistentData() {
  if (!managers_initialized || !IsStoragePersistenceEnabled())
    return;
  std::error_code ec;
  std::filesystem::create_directories(kStorageDirectory, ec);
  if (ec || !std::filesystem::exists(kStorageDirectory, ec))
    return;
  hotlist_export(kHotlistFilePath, nullptr);
  urldb_save(kUrlsFilePath);
  urldb_save_cookies(kCookiesFilePath);
}

void FinalizeManagers() {
  if (!managers_initialized)
    return;

  SavePersistentData();

  cookie_manager_fini();
  global_history_fini();
  hotlist_manager_fini();
  hotlist_fini();
  treeview_fini();

  bookmarks_ui_window.reset();
  bookmarks_window_node.reset();
  history_ui_window.reset();
  history_window_node.reset();
  cookies_ui_window.reset();
  cookies_window_node.reset();
  bookmarks_host.reset();
  history_host.reset();
  cookies_host.reset();

  managers_initialized = false;
}

void ShowBookmarksWindow() {
  if (!managers_initialized)
    InitializeManagers();

  if (bookmarks_ui_window) {
    bookmarks_ui_window->Focus();
    return;
  }

  DetachHostRootNode(bookmarks_host);

  auto search_node = InputBox::BasicInputBox(
      "", &bookmarks_search_box,
      [](Layout& layout) { layout.SetFlexGrow(1.0f); },
      [](InputBox& input) {
        input.OnTextChanged([](std::string_view val) {
          NETSURF_LOCK;
          if (hl_ctx.tree != nullptr) {
            std::string s(val);
            treeview_set_search_string(hl_ctx.tree,
                                       s.empty() ? nullptr : s.c_str());
          }
        });
      });

  auto add_bookmark_icon = Image::LoadImage(kBookmarkPlusIconPath);
  auto new_folder_icon = Image::LoadImage(kFolderPlusIconPath);
  auto edit_icon = Image::LoadImage(kEditIconPath);
  auto trash_icon = Image::LoadImage(kTrashIconPath);
  auto expand_icon = Image::LoadImage(kExpandAllIconPath);
  auto collapse_icon = Image::LoadImage(kCollapseAllIconPath);

  auto toolbar = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetFlexShrink(0.0f);
      },
      ImageButton::BasicImageButton(
          []() {
            NETSURF_LOCK;
            Window* gw = GetActiveTab();
            struct nsurl* active_url = nullptr;
            const char* active_title = nullptr;
            if (gw != nullptr && gw->GetBrowserWindow() != nullptr) {
              browser_window_get_url(gw->GetBrowserWindow(), true, &active_url);
              active_title = browser_window_get_title(gw->GetBrowserWindow());
            }
            if (active_url != nullptr) {
              hotlist_add_entry(active_url, active_title, false, 0);
              nsurl_unref(active_url);
            } else {
              hotlist_add_entry(nullptr, nullptr, false, 0);
            }
            SavePersistentData();
          },
          add_bookmark_icon, Tooltip::ShowTooltip("Add Bookmark")),
      ImageButton::BasicImageButton(
          []() {
            NETSURF_LOCK;
            hotlist_add_folder(nullptr, false, 0);
            SavePersistentData();
          },
          new_folder_icon, Tooltip::ShowTooltip("New Folder")),
      ImageButton::BasicImageButton(
          []() {
            NETSURF_LOCK;
            hotlist_edit_selection();
          },
          edit_icon, Tooltip::ShowTooltip("Edit Selected")),
      ImageButton::BasicImageButton(
          []() {
            NETSURF_LOCK;
            hotlist_keypress(NS_KEY_DELETE_LEFT);
            SavePersistentData();
          },
          trash_icon, Tooltip::ShowTooltip("Delete Selected")),
      ImageButton::BasicImageButton(
          []() {
            NETSURF_LOCK;
            hotlist_expand(false);
          },
          expand_icon, Tooltip::ShowTooltip("Expand All")),
      ImageButton::BasicImageButton(
          []() {
            NETSURF_LOCK;
            hotlist_contract(true);
          },
          collapse_icon, Tooltip::ShowTooltip("Collapse All")),
      search_node);

  bookmarks_window_node = UiWindow::ResizableWindowWithTitleBar(
      "Bookmarks", &bookmarks_ui_window,
      [](Layout& layout) {
        layout.SetWidth(kBookmarksWindowWidth);
        layout.SetHeight(kBookmarksWindowHeight);
      },
      [](UiWindow& window) {
        window.OnClose([]() {
          NETSURF_LOCK;
          if (hl_ctx.tree != nullptr)
            treeview_set_search_string(hl_ctx.tree, nullptr);
          DetachHostRootNode(bookmarks_host);
          bookmarks_search_box.reset();
          bookmarks_ui_window.reset();
          auto old_node = std::move(bookmarks_window_node);
          ::perception::Defer([old_node]() {});
        });
      },
      toolbar, bookmarks_host->GetRootNode());
}

void ShowGlobalHistoryWindow() {
  if (!managers_initialized)
    InitializeManagers();

  if (history_ui_window) {
    history_ui_window->Focus();
    return;
  }

  DetachHostRootNode(history_host);

  auto search_node = InputBox::BasicInputBox(
      "", &history_search_box,
      [](Layout& layout) { layout.SetFlexGrow(1.0f); },
      [](InputBox& input) {
        input.OnTextChanged([](std::string_view val) {
          NETSURF_LOCK;
          if (gh_ctx.tree != nullptr) {
            std::string s(val);
            treeview_set_search_string(gh_ctx.tree,
                                       s.empty() ? nullptr : s.c_str());
          }
        });
      });

  auto trash_icon = Image::LoadImage(kTrashIconPath);
  auto clear_all_icon = Image::LoadImage(kClearAllIconPath);
  auto expand_icon = Image::LoadImage(kExpandAllIconPath);
  auto collapse_icon = Image::LoadImage(kCollapseAllIconPath);

  auto toolbar = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetFlexShrink(0.0f);
      },
      ImageButton::BasicImageButton(
          []() {
            NETSURF_LOCK;
            global_history_keypress(NS_KEY_DELETE_LEFT);
            SavePersistentData();
          },
          trash_icon, Tooltip::ShowTooltip("Delete Selected")),
      ImageButton::BasicImageButton(
          []() {
            NETSURF_LOCK;
            global_history_keypress(NS_KEY_SELECT_ALL);
            global_history_keypress(NS_KEY_DELETE_LEFT);
            SavePersistentData();
          },
          clear_all_icon, Tooltip::ShowTooltip("Clear All History")),
      ImageButton::BasicImageButton(
          []() {
            NETSURF_LOCK;
            global_history_expand(false);
          },
          expand_icon, Tooltip::ShowTooltip("Expand All")),
      ImageButton::BasicImageButton(
          []() {
            NETSURF_LOCK;
            global_history_contract(true);
          },
          collapse_icon, Tooltip::ShowTooltip("Collapse All")),
      search_node);

  history_window_node = UiWindow::ResizableWindowWithTitleBar(
      "History", &history_ui_window,
      [](Layout& layout) {
        layout.SetWidth(kHistoryWindowWidth);
        layout.SetHeight(kHistoryWindowHeight);
      },
      [](UiWindow& window) {
        window.OnClose([]() {
          NETSURF_LOCK;
          if (gh_ctx.tree != nullptr)
            treeview_set_search_string(gh_ctx.tree, nullptr);
          DetachHostRootNode(history_host);
          history_search_box.reset();
          history_ui_window.reset();
          auto old_node = std::move(history_window_node);
          ::perception::Defer([old_node]() {});
        });
      },
      toolbar, history_host->GetRootNode());
}

void ShowCookieManagerWindow(const char* search_filter) {
  if (!managers_initialized)
    InitializeManagers();

  std::string initial_filter = search_filter != nullptr ? search_filter : "";

  if (cookies_ui_window) {
    if (search_filter != nullptr) {
      if (cookies_search_box)
        cookies_search_box->SetText(initial_filter);
      NETSURF_LOCK;
      cookie_manager_set_search_string(
          initial_filter.empty() ? nullptr : initial_filter.c_str());
    }
    cookies_ui_window->Focus();
    return;
  }

  DetachHostRootNode(cookies_host);

  auto search_node = InputBox::BasicInputBox(
      initial_filter, &cookies_search_box,
      [](Layout& layout) { layout.SetFlexGrow(1.0f); },
      [](InputBox& input) {
        input.OnTextChanged([](std::string_view val) {
          NETSURF_LOCK;
          std::string s(val);
          cookie_manager_set_search_string(s.empty() ? nullptr : s.c_str());
        });
      });

  {
    NETSURF_LOCK;
    cookie_manager_set_search_string(
        initial_filter.empty() ? nullptr : initial_filter.c_str());
  }

  auto trash_icon = Image::LoadImage(kTrashIconPath);
  auto expand_icon = Image::LoadImage(kExpandAllIconPath);
  auto collapse_icon = Image::LoadImage(kCollapseAllIconPath);

  auto toolbar = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetFlexShrink(0.0f);
      },
      ImageButton::BasicImageButton(
          []() {
            NETSURF_LOCK;
            cookie_manager_keypress(NS_KEY_DELETE_LEFT);
            SavePersistentData();
          },
          trash_icon, Tooltip::ShowTooltip("Delete Selected")),
      ImageButton::BasicImageButton(
          []() {
            NETSURF_LOCK;
            cookie_manager_expand(false);
          },
          expand_icon, Tooltip::ShowTooltip("Expand All")),
      ImageButton::BasicImageButton(
          []() {
            NETSURF_LOCK;
            cookie_manager_contract(true);
          },
          collapse_icon, Tooltip::ShowTooltip("Collapse All")),
      search_node);

  cookies_window_node = UiWindow::ResizableWindowWithTitleBar(
      "Cookies", &cookies_ui_window,
      [](Layout& layout) {
        layout.SetWidth(kCookieWindowWidth);
        layout.SetHeight(kCookieWindowHeight);
      },
      [](UiWindow& window) {
        window.OnClose([]() {
          NETSURF_LOCK;
          cookie_manager_set_search_string(nullptr);
          DetachHostRootNode(cookies_host);
          cookies_search_box.reset();
          cookies_ui_window.reset();
          auto old_node = std::move(cookies_window_node);
          ::perception::Defer([old_node]() {});
        });
      },
      toolbar, cookies_host->GetRootNode());
}

void ShowLocalHistoryPopup(struct browser_window* bw,
                           std::shared_ptr<Node> anchor_node) {
  if (bw == nullptr || !anchor_node)
    return;

  auto session_holder =
      std::make_shared<struct local_history_session*>(nullptr);
  auto host_holder = std::make_shared<std::weak_ptr<CoreWindowHost>>();

  auto host = CoreWindowHost::Create(
      [session_holder](const struct rect& clip,
                       const struct redraw_context& ctx) {
        if (*session_holder != nullptr)
          local_history_redraw(*session_holder, 0, 0,
                               const_cast<struct rect*>(&clip), &ctx);
      },
      [session_holder, host_holder](browser_mouse_state mouse, int x, int y) {
        if (*session_holder == nullptr)
          return;
        if (local_history_mouse_action(*session_holder, mouse, x, y) ==
            NSERROR_OK)
          if (auto h = host_holder->lock())
            PopUp::Close(h->GetRootNode());
      },
      [session_holder](uint32_t nskey) -> bool {
        if (*session_holder == nullptr)
          return false;
        return local_history_keypress(*session_holder, nskey);
      });
  *host_holder = host;

  int width = 0;
  int height = 0;
  {
    NETSURF_LOCK;
    if (local_history_init(host->GetCoreWindow(), bw, session_holder.get()) !=
        NSERROR_OK)
      return;
    local_history_get_size(*session_holder, &width, &height);
  }

  float popup_w = width > 0 ? std::min(static_cast<float>(width), kMaxPopupWidth)
                            : kLocalHistoryFallbackWidth;
  float popup_h =
      height > 0 ? std::min(static_cast<float>(height), kMaxPopupHeight)
                 : kLocalHistoryFallbackHeight;

  host->GetRootNode()->Apply([popup_w, popup_h](Layout& layout) {
    layout.SetWidth(popup_w);
    layout.SetHeight(popup_h);
  });

  auto popup_box = Container::VerticalContainer(
      [](Block& block) {
        block.SetFillColor(::perception::ui::kPopUpMenuBackgroundColor);
        block.SetBorderColor(::perception::ui::kPopUpMenuBorderColor);
        block.SetBorderWidth(kPopupBorderWidth);
        block.SetBorderRadius(kPopupBorderRadius);
        block.SetClipContents(true);
      },
      [popup_w, popup_h](Layout& layout) {
        layout.SetWidth(popup_w);
        layout.SetHeight(popup_h);
        layout.SetPadding(YGEdgeAll, 0.0f);
        layout.SetGap(0.0f);
      },
      host->GetRootNode());

  Point pos = anchor_node->GetAbsolutePosition();
  Size size = anchor_node->GetSize();
  Point anchor{pos.x, pos.y + size.height};

  PopUp::Show(anchor_node, anchor, popup_box, [session_holder, host]() {
    NETSURF_LOCK;
    if (*session_holder != nullptr) {
      local_history_fini(*session_holder);
      *session_holder = nullptr;
    }
  });
}

void ShowPageInfoPopup(struct browser_window* bw,
                       std::shared_ptr<Node> anchor_node) {
  if (bw == nullptr || !anchor_node)
    return;

  if (!managers_initialized)
    InitializeManagers();

  auto pi_holder = std::make_shared<struct page_info*>(nullptr);

  auto host = CoreWindowHost::Create(
      [pi_holder](const struct rect& clip, const struct redraw_context& ctx) {
        if (*pi_holder != nullptr)
          page_info_redraw(*pi_holder, 0, 0, const_cast<struct rect*>(&clip),
                           &ctx);
      },
      [pi_holder](browser_mouse_state mouse, int x, int y) {
        if (*pi_holder == nullptr)
          return;
        bool did_something = false;
        page_info_mouse_action(*pi_holder, mouse, x, y, &did_something);
      },
      [pi_holder](uint32_t nskey) -> bool {
        if (*pi_holder == nullptr)
          return false;
        return page_info_keypress(*pi_holder, nskey);
      });

  int width = 0;
  int height = 0;
  {
    NETSURF_LOCK;
    if (page_info_create(host->GetCoreWindow(), bw, pi_holder.get()) !=
        NSERROR_OK)
      return;
    page_info_get_size(*pi_holder, &width, &height);
  }

  float popup_w = width > 0 ? std::min(static_cast<float>(width), kMaxPopupWidth)
                            : kPageInfoFallbackWidth;
  float popup_h =
      height > 0 ? std::min(static_cast<float>(height), kMaxPopupHeight)
                 : kPageInfoFallbackHeight;

  host->GetRootNode()->Apply([popup_w, popup_h](Layout& layout) {
    layout.SetWidth(popup_w);
    layout.SetHeight(popup_h);
  });

  auto popup_box = Container::VerticalContainer(
      [](Block& block) {
        block.SetFillColor(::perception::ui::kPopUpMenuBackgroundColor);
        block.SetBorderColor(::perception::ui::kPopUpMenuBorderColor);
        block.SetBorderWidth(kPopupBorderWidth);
        block.SetBorderRadius(kPopupBorderRadius);
        block.SetClipContents(true);
      },
      [popup_w, popup_h](Layout& layout) {
        layout.SetWidth(popup_w);
        layout.SetHeight(popup_h);
        layout.SetPadding(YGEdgeAll, 0.0f);
        layout.SetGap(0.0f);
      },
      host->GetRootNode());

  Point pos = anchor_node->GetAbsolutePosition();
  Size size = anchor_node->GetSize();
  Point anchor{pos.x, pos.y + size.height};

  PopUp::Show(anchor_node, anchor, popup_box, [pi_holder, host]() {
    NETSURF_LOCK;
    if (*pi_holder != nullptr) {
      page_info_destroy(*pi_holder);
      *pi_holder = nullptr;
    }
  });
}

bool IsUrlBookmarked(struct nsurl* url) {
  if (url == nullptr)
    return false;
  if (!managers_initialized)
    InitializeManagers();
  return hotlist_has_url(url);
}

void ToggleBookmarkForUrl(struct nsurl* url) {
  if (url == nullptr)
    return;
  if (!managers_initialized)
    InitializeManagers();
  if (hotlist_has_url(url)) {
    hotlist_remove_url(url);
  } else {
    hotlist_add_url(url);
  }
  SavePersistentData();
}

void AddBookmarkDialog(struct nsurl* url, std::string_view current_title,
                       std::shared_ptr<Node> parent_window) {
  if (url == nullptr)
    return;
  if (!managers_initialized)
    InitializeManagers();

  std::shared_ptr<struct nsurl> retained_url(
      nsurl_ref(url), [](struct nsurl* u) {
        if (u != nullptr)
          nsurl_unref(u);
      });

  std::string initial_title(current_title);
  {
    NETSURF_LOCK;
    if (initial_title.empty()) {
      const struct url_data* data = urldb_get_url_data(retained_url.get());
      if (data != nullptr && data->title != nullptr)
        initial_title = data->title;
      else if (nsurl_access(retained_url.get()) != nullptr)
        initial_title = nsurl_access(retained_url.get());
    }
    if (!hotlist_has_url(retained_url.get())) {
      if (!initial_title.empty())
        urldb_set_url_title(retained_url.get(), initial_title.c_str());
      hotlist_add_url(retained_url.get());
      SavePersistentData();
    }
  }

  std::shared_ptr<InputBox> title_box;
  auto title_node = InputBox::BasicInputBox(
      initial_title, &title_box,
      [](Layout& layout) { layout.SetFlexGrow(1.0f); });

  auto window_holder = std::make_shared<std::shared_ptr<Node>>();
  auto closed = std::make_shared<bool>(false);
  auto close_dialog = [window_holder, closed]() {
    if (*closed)
      return;
    *closed = true;
    auto node = *window_holder;
    if (!node)
      return;
    if (auto uw = node->Get<UiWindow>())
      uw->Close();
    ::perception::Defer([window_holder]() { window_holder->reset(); });
  };

  auto done_button = Button::TextButton(
      "Done", [retained_url, title_box, close_dialog]() {
        {
          NETSURF_LOCK;
          std::string updated_title =
              title_box ? title_box->GetText() : std::string();
          if (!updated_title.empty())
            urldb_set_url_title(retained_url.get(), updated_title.c_str());
          if (!hotlist_has_url(retained_url.get()))
            hotlist_add_url(retained_url.get());
          else
            hotlist_update_url(retained_url.get());
          SavePersistentData();
        }
        close_dialog();
      },
      [](Button& button) {
        button.SetButtonStyle(Button::ButtonStyle::PRIMARY);
      });

  auto remove_button = Button::TextButton(
      "Remove", [retained_url, close_dialog]() {
        {
          NETSURF_LOCK;
          if (hotlist_has_url(retained_url.get()))
            hotlist_remove_url(retained_url.get());
          SavePersistentData();
        }
        close_dialog();
      });

  std::shared_ptr<Node> effective_parent =
      parent_window ? parent_window : GetGlobalUiWindow();

  auto dialog = UiWindow::DialogWithTitleBar(
      "Bookmark", UiWindow::Parent(effective_parent),
      [close_dialog](UiWindow& window) {
        window.OnClose([close_dialog]() { close_dialog(); });
      },
      [](Layout& layout) {
        layout.SetWidth(kBookmarkDialogWidth);
        layout.SetHeight(kBookmarkDialogHeight);
      },
      Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetAlignItems(YGAlignCenter);
          },
          Label::BasicLabel("Name:"), title_node),
      Node::Empty([](Layout& layout) { layout.SetFlexGrow(1.0f); }),
      Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetJustifyContent(YGJustifyFlexEnd);
            layout.SetAlignItems(YGAlignCenter);
          },
          remove_button, done_button));
  *window_holder = dialog;
}

}  // namespace perception
}  // namespace netsurf
