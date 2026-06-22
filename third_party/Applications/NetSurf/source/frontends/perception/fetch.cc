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

#include "fetch.h"

#include <limits.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

extern "C" {
#include "utils/errors.h"
#include "netsurf/fetch.h"
#include "utils/file.h"
#include "utils/filepath.h"
#include "utils/nsurl.h"
}

namespace netsurf {
namespace perception {
namespace {

char** internal_respaths = nullptr;

const char* FileType(const char* unix_path) {
  std::string_view path(unix_path);
  size_t dot = path.find_last_of('.');
  if (dot != std::string_view::npos) {
    std::string_view ext = path.substr(dot + 1);
    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "css") return "text/css";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "txt") return "text/plain";
  }
  return "application/octet-stream";
}

struct nsurl* GetResourceUrl(const char* path) {
  char buf[PATH_MAX];
  struct nsurl* url = nullptr;

  if (strcmp(path, "favicon.ico") == 0) {
    path = "favicon.png";
  }

  char* found_path = filepath_sfind((char**)GetResourcePaths(), buf, path);
  if (found_path != nullptr) {
    netsurf_path_to_nsurl(found_path, &url);
  } else {
    std::string fallback_path =
        "file:///Applications/netsurf/res/" + std::string(path);
    nsurl_create(fallback_path.c_str(), &url);
  }
  return url;
}

const char* get_language_env(void) {
  const char* envstr;

  envstr = getenv("LANGUAGE");
  if ((envstr != NULL) && (envstr[0] != 0)) {
    return envstr;
  }

  envstr = getenv("LC_ALL");
  if ((envstr != NULL) && (envstr[0] != 0)) {
    return envstr;
  }

  envstr = getenv("LC_MESSAGES");
  if ((envstr != NULL) && (envstr[0] != 0)) {
    return envstr;
  }

  envstr = getenv("LANG");
  if ((envstr != NULL) && (envstr[0] != 0)) {
    return envstr;
  }

  return "en";
}

char** get_language_names(void) {
  char** langv;         // output string vector of languages
  int langc;            // count of languages in vector
  const char* envlang;  // colon separated list of languages from environment
  int lstart = 0;       // offset to start of current language
  int lunder = 0;       // offset to underscore in current language
  int lend = 0;         // offset to end of current language
  char* nlang;

  langv = (char**)calloc(32 + 2, sizeof(char*));
  if (langv == NULL) {
    return NULL;
  }

  envlang = get_language_env();

  for (langc = 0; langc < 32; langc++) {
    // Work through envlang splitting on :.
    while ((envlang[lend] != 0) && (envlang[lend] != ':') &&
           (envlang[lend] != '.')) {
      if (envlang[lend] == '_') {
        lunder = lend;
      }
      lend++;
    }
    // place language in string vector
    nlang = (char*)malloc(lend - lstart + 1);
    memcpy(nlang, envlang + lstart, lend - lstart);
    nlang[lend - lstart] = 0;
    langv[langc] = nlang;

    // add language without specialisation to vector
    if (lunder != lstart) {
      nlang = (char*)malloc(lunder - lstart + 1);
      memcpy(nlang, envlang + lstart, lunder - lstart);
      nlang[lunder - lstart] = 0;
      langv[++langc] = nlang;
    }

    // if we stopped at the dot, move to the colon delimiter
    while ((envlang[lend] != 0) && (envlang[lend] != ':')) {
      lend++;
    }
    if (envlang[lend] == 0) {
      // reached end of environment language list
      break;
    }
    lend++;
    lstart = lunder = lend;
  }
  return langv;
}

}  // namespace

const char** GetResourcePaths() { return (const char**)internal_respaths; }

char** FbInitResourcePath(const char* resource_path) {
  char* pathv[2];
  pathv[0] = (char*)"/Applications/netsurf/res";
  pathv[1] = NULL;

  char** langv = get_language_names();
  char** respath = filepath_generate(pathv, (const char* const*)langv);

  if (langv) {
    for (int i = 0; langv[i] != NULL; i++) {
      free(langv[i]);
    }
    free(langv);
  }

  internal_respaths = respath;
  return respath;
}

struct gui_fetch_table perception_fetch_table = {
    .filetype = FileType,
    .get_resource_url = GetResourceUrl,
};

}  // namespace perception
}  // namespace netsurf
