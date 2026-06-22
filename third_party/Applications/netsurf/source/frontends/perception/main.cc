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

extern "C" {
#include "desktop/browser_history.h"
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
#include "perception/debug.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "settings.h"

using ::perception::HandOverControl;

namespace {

bool NslogStreamConfigure(FILE* fptr) {
  setbuf(fptr, NULL);
  return true;
}

bool ProcessCmdline(int argc, char* argv[]) {
  if (argc > 1) feurl = argv[1];
  return true;
}

void Die(const char* msg) {
  fprintf(stderr, "FATAL ERROR: %s\n", msg);
  exit(1);
}

nserror SetDefaults(struct nsoption_s* defaults) {
  int idx;
  static const struct {
    enum nsoption_e nsc;
    colour c;
  } sys_colour_defaults[] = {
      {NSOPTION_sys_colour_AccentColor, 0x00666666},
      {NSOPTION_sys_colour_AccentColorText, 0x00ffffff},
      {NSOPTION_sys_colour_ActiveText, 0x000000ee},
      {NSOPTION_sys_colour_ButtonBorder, 0x00aaaaaa},
      {NSOPTION_sys_colour_ButtonFace, 0x00dddddd},
      {NSOPTION_sys_colour_ButtonText, 0x00000000},
      {NSOPTION_sys_colour_Canvas, 0x00aaaaaa},
      {NSOPTION_sys_colour_CanvasText, 0x00000000},
      {NSOPTION_sys_colour_Field, 0x00f1f1f1},
      {NSOPTION_sys_colour_FieldText, 0x00000000},
      {NSOPTION_sys_colour_GrayText, 0x00777777},
      {NSOPTION_sys_colour_Highlight, 0x00ee0000},
      {NSOPTION_sys_colour_HighlightText, 0x00000000},
      {NSOPTION_sys_colour_LinkText, 0x00ee0000},
      {NSOPTION_sys_colour_Mark, 0x0000ffff},
      {NSOPTION_sys_colour_MarkText, 0x00000000},
      {NSOPTION_sys_colour_SelectedItem, 0x00e48435},
      {NSOPTION_sys_colour_SelectedItemText, 0x00ffffff},
      {NSOPTION_sys_colour_VisitedText, 0x008b1a55},
      {NSOPTION_LISTEND, 0},
  };

  nsoption_setnull_charp(cookie_file, strdup("~/.netsurf/Cookies"));
  nsoption_setnull_charp(cookie_jar, strdup("~/.netsurf/Cookies"));

  if (nsoption_charp(cookie_file) == NULL ||
      nsoption_charp(cookie_jar) == NULL) {
    NSLOG(netsurf, INFO, "Failed initialising cookie options");
    return NSERROR_BAD_PARAMETER;
  }

  for (idx = 0; sys_colour_defaults[idx].nsc != NSOPTION_LISTEND; idx++) {
    defaults[sys_colour_defaults[idx].nsc].value.c = sys_colour_defaults[idx].c;
  }
  return NSERROR_OK;
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
      .clipboard = &perception_clipboard_table,
      .fetch = &perception_fetch_table,
      .utf8 = NULL,
      .bitmap = &skia_bitmap_table,
      .layout = &skia_layout_table,
  };

  ret = netsurf_register(&perception_table);
  if (ret != NSERROR_OK) {
    Die("NetSurf operation table failed registration");
  }

  respaths = netsurf::perception::FbInitResourcePath(nullptr);

  nslog_init(NslogStreamConfigure, &argc, argv);

  /* user options setup */
  ret = nsoption_init(SetDefaults, &nsoptions, &nsoptions_default);
  if (ret != NSERROR_OK) {
    Die("Options failed to initialise");
  }
  options = filepath_find(respaths, "Choices");
  nsoption_read(options, nsoptions);
  free(options);
  nsoption_commandline(&argc, argv, nsoptions);

  /* message init */
  messages = filepath_find(respaths, "FatMessages");
  if (messages) {
    ::perception::DebugPrinterSingleton
        << "NetSurf Native: Found FatMessages at " << messages << "\n";
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

  /* common initialisation */
  ret = netsurf_init(NULL);
  if (ret != NSERROR_OK) {
    Die("NetSurf failed to initialise");
  }

  ::netsurf::perception::RegisterPerceptionHttpFetcher();

  /* Override, since we have no support for non-core SELECT menu */
  nsoption_set_bool(core_select_menu, true);

  /* Disable JavaScript by default so <noscript> fallbacks render correctly */
  nsoption_set_bool(enable_javascript, false);

  ::netsurf::perception::LoadSettingsFromRegistry();

  if (!ProcessCmdline(argc, argv)) Die("unable to process command line.\n");

  urldb_load_cookies(nsoption_charp(cookie_file));

  ret = nsurl_create(feurl, &url);
  if (ret == NSERROR_OK) {
    ret = browser_window_create(BW_CREATE_HISTORY, url, NULL, NULL, &bw);
    nsurl_unref(url);
  }
  if (ret != NSERROR_OK) {
    fprintf(stderr, "Error: %s\n", messages_get_errorcode(ret));
  }

  HandOverControl();
  return 0;
}
