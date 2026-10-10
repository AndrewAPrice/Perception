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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmath>
#include <iostream>
#include <memory>

extern "C" {
#include "desktop/browser_history.h"
#include "desktop/save_complete.h"
#include "netsurf/bitmap.h"
#include "netsurf/browser.h"
#include "netsurf/browser_window.h"
#include "netsurf/cookie_db.h"
#include "netsurf/layout.h"
#include "netsurf/misc.h"
#include "netsurf/netsurf.h"
#include "netsurf/window.h"
#include "utils/filepath.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"
}

#include "gui.h"
#include "http.h"
#include "managers.h"
#include "misc.h"
#include "network_log.h"
#include "perception/debug.h"
#include "perception/fibers.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/window/window_manager.h"
#include "settings.h"
#include "tabs.h"
#include "window.h"

using ::perception::HandOverControl;

namespace {

// Default display DPI for NetSurf.
constexpr int kDefaultDpi = 90;

// Minimum display scale to consider valid.
constexpr float kMinValidScale = 0.5f;

bool NslogStreamConfigure(FILE* fptr) {
  setbuf(fptr, NULL);
  return true;
}

bool ProcessCmdline(int argc, char* argv[]) {
  if (argc > 1) {
    ::netsurf::perception::SetInitialUrl(argv[1]);
  } else {
    const char* homepage = nsoption_charp(homepage_url);
    if (homepage != nullptr && homepage[0] != '\0')
      ::netsurf::perception::SetInitialUrl(homepage);
  }
  return true;
}

void Die(const char* msg) {
  fprintf(stderr, "FATAL ERROR: %s\n", msg);
  exit(1);
}

nserror SetDefaults(struct nsoption_s* defaults) {
  nsoption_setnull_charp(cookie_file, strdup("~/.netsurf/Cookies"));
  nsoption_setnull_charp(cookie_jar, strdup("~/.netsurf/Cookies"));

  if (nsoption_charp(cookie_file) == NULL ||
       nsoption_charp(cookie_jar) == NULL) {
    NSLOG(netsurf, INFO, "Failed initialising cookie options");
    return NSERROR_BAD_PARAMETER;
  }

  ::netsurf::perception::SetSystemColorDefaults(defaults);
  return NSERROR_OK;
}

class NetSurfWindowManagerEnvironmentListener
    : public ::perception::window::WindowManagerEnvironmentListener::Server {
 public:
  explicit NetSurfWindowManagerEnvironmentListener(int base_dpi)
      : base_dpi_(base_dpi) {}

  Status WindowManagerEnvironmentChanged(
      const ::perception::window::WindowManagerEnvironmentChangedNotification&
          notification) override {
    float scale = notification.scale;
    if (scale < kMinValidScale)
      scale = 1.0f;
    int scaled_dpi = static_cast<int>(std::lround(base_dpi_ * scale));
    ::perception::Defer([scaled_dpi]() {
      NETSURF_LOCK;
      browser_set_dpi(scaled_dpi);
      for (auto* gw : ::netsurf::perception::GetOpenTabs()) {
        if (gw && gw->GetBrowserWindow()) {
          browser_window_schedule_reformat(gw->GetBrowserWindow());
          if (gw->GetContentNode())
            gw->GetContentNode()->Invalidate();
        }
      }
    });
    return Status::OK;
  }

 private:
  int base_dpi_;
};

std::unique_ptr<NetSurfWindowManagerEnvironmentListener>&
GetNetSurfEnvironmentListener() {
  static std::unique_ptr<NetSurfWindowManagerEnvironmentListener> listener;
  return listener;
}

void InitializeDpi() {
  const int default_dpi = browser_get_dpi();
  const int base_dpi = default_dpi > 0 ? default_dpi : kDefaultDpi;

  float scale = 1.0f;
  auto window_manager = ::perception::FindFirstInstanceOfService<
      ::perception::window::WindowManager>();
  if (window_manager) {
    auto env = window_manager->GetDisplayEnvironment();
    if (env.Ok() && env->scale >= kMinValidScale) {
      scale = env->scale;
    } else {
      auto env_resp = window_manager->GetEnvironment();
      if (env_resp.Ok() && env_resp->scale >= kMinValidScale)
        scale = env_resp->scale;
    }
  }

  int scaled_dpi = static_cast<int>(std::lround(base_dpi * scale));
  browser_set_dpi(scaled_dpi);

  GetNetSurfEnvironmentListener() =
      std::make_unique<NetSurfWindowManagerEnvironmentListener>(base_dpi);
}

}  // namespace

int main(int argc, char* argv[]) {
  struct browser_window* bw;
  char* options;
  char* messages;
  nsurl* url;
  nserror ret;

  struct netsurf_table perception_table = {
      .misc = &perception_misc_table,
      .window = &perception_window_table,
      .corewindow = &perception_core_window_table,
      .download = &perception_download_table,
      .clipboard = &perception_clipboard_table,
      .fetch = &perception_fetch_table,
      .file = NULL,
      .utf8 = NULL,
      .search = &perception_search_table,
      .search_web = NULL,
      .llcache = NULL,
      .bitmap = &skia_bitmap_table,
      .layout = &skia_layout_table,
  };

  ret = netsurf_register(&perception_table);
  if (ret != NSERROR_OK)
    Die("NetSurf operation table failed registration");

  bitmap_fmt_t bfmt = {
      .layout = BITMAP_LAYOUT_R8G8B8A8,
      .pma = true,
  };
  bitmap_set_format(&bfmt);

  netsurf::perception::FbInitResourcePath(nullptr);

  nslog_init(NslogStreamConfigure, &argc, argv);

  /* user options setup */
  ret = nsoption_init(SetDefaults, &nsoptions, &nsoptions_default);
  if (ret != NSERROR_OK)
    Die("Options failed to initialise");
  options =
      filepath_find((char**)netsurf::perception::GetResourcePaths(), "Choices");
  nsoption_read(options, nsoptions);
  free(options);
  nsoption_commandline(&argc, argv, nsoptions);

  /* message init */
  messages = filepath_find((char**)netsurf::perception::GetResourcePaths(),
                           "FatMessages");
  if (messages) {
    ret = messages_add_from_file(messages);
    free(messages);
  } else {
    ::perception::DebugPrinterSingleton
        << "NetSurf Native: Could not find FatMessages\n";
    ret = NSERROR_NOT_FOUND;
  }
  if (ret != NSERROR_OK) {
    ::perception::DebugPrinterSingleton
        << "NetSurf Native: Message translations failed to load: " << (int64)ret
        << "\n";
  }

  {
    NETSURF_LOCK;
    /* common initialisation */
    ret = netsurf_init(NULL);
    if (ret != NSERROR_OK)
      Die("NetSurf failed to initialise");

    InitializeDpi();

    ::netsurf::perception::InitializeNetworkLog();
    ::netsurf::perception::RegisterPerceptionHttpFetcher();

    /* Override, since only core SELECT menu is supported */
    nsoption_set_bool(core_select_menu, true);

    /* Enable JavaScript by default */
    nsoption_set_bool(enable_javascript, true);

    ::netsurf::perception::LoadSettingsFromRegistry();
    ::netsurf::perception::InitializeManagers();
    save_complete_init();

    if (!ProcessCmdline(argc, argv))
      Die("unable to process command line.\n");

    urldb_load_cookies(nsoption_charp(cookie_file));

    ret = nsurl_create(netsurf::perception::GetInitialUrl(), &url);
    if (ret == NSERROR_OK) {
      ret = browser_window_create(BW_CREATE_HISTORY, url, NULL, NULL, &bw);
      nsurl_unref(url);
    }
    if (ret != NSERROR_OK)
      fprintf(stderr, "Error: %s\n", messages_get_errorcode(ret));
  }

  HandOverControl();
  return 0;
}
