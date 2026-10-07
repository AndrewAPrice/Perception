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

#pragma once

#include <memory>
#include <string_view>

struct browser_window;
struct nsurl;

namespace perception {
namespace ui {
class Node;
}  // namespace ui
}  // namespace perception

namespace netsurf {
namespace perception {

// Initializes the built-in NetSurf corewindow managers (bookmarks, history,
// and cookies) and loads persistent data from disk if enabled.
void InitializeManagers();

// Finalizes all built-in NetSurf corewindow managers and saves persistent data
// if enabled.
void FinalizeManagers();

// Saves bookmarks, URL history, and cookies to disk if storage persistence is
// enabled.
void SavePersistentData();

// Opens or focuses the Bookmarks (Hotlist) manager window.
void ShowBookmarksWindow();

// Opens or focuses the Global History manager window.
void ShowGlobalHistoryWindow();

// Opens or focuses the Cookie Manager window, optionally filtering by domain
// or search string.
void ShowCookieManagerWindow(const char* search_filter = nullptr);

// Displays a local back/forward history popup anchored to the given UI node.
void ShowLocalHistoryPopup(
    struct browser_window* bw,
    std::shared_ptr<::perception::ui::Node> anchor_node);

// Displays a page security and certificate info popup anchored to the given UI
// node.
void ShowPageInfoPopup(struct browser_window* bw,
                       std::shared_ptr<::perception::ui::Node> anchor_node);

// Returns whether the specified URL is currently in the bookmarks hotlist.
bool IsUrlBookmarked(struct nsurl* url);

// Toggles whether the specified URL is bookmarked in the hotlist.
void ToggleBookmarkForUrl(struct nsurl* url);

// Opens a dialog allowing the user to edit the title or remove a bookmark.
void AddBookmarkDialog(
    struct nsurl* url, std::string_view current_title = "",
    std::shared_ptr<::perception::ui::Node> parent_window = nullptr);

}  // namespace perception
}  // namespace netsurf
