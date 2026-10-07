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

#include "resources.h"

#include <string_view>
#include <unordered_set>
#include <utility>

extern "C" {
#include <dom/core/string.h>

#include "utils/errors.h"
#include "content/content.h"
#define restrict __restrict__
#include "content/handlers/css/css.h"
#undef restrict
#include "content/handlers/html/html.h"
#include "content/hlcache.h"
#include "desktop/browser_private.h"
#include "netsurf/browser_window.h"
#include "netsurf/content.h"
#include "netsurf/content_type.h"
#include "utils/nsurl.h"
}

namespace netsurf::perception {

namespace {

// Display label for the main document resource.
constexpr std::string_view kMainDocumentFallbackName = "(main document)";

// Display label for a sub-frame or iframe document resource.
constexpr std::string_view kFrameDocumentFallbackName = "(frame document)";

// Display label for the built-in user agent base stylesheet.
constexpr std::string_view kBaseStylesheetName = "(user agent stylesheet)";

// Display label for the built-in quirks mode stylesheet.
constexpr std::string_view kQuirksStylesheetName = "(quirks stylesheet)";

// Display label for the built-in adblock stylesheet.
constexpr std::string_view kAdblockStylesheetName = "(adblock stylesheet)";

// Display label for the user stylesheet.
constexpr std::string_view kUserStylesheetName = "(user stylesheet)";

// Default MIME type assigned to inline JavaScript blocks.
constexpr std::string_view kDefaultScriptMimeType = "application/javascript";

std::string DeriveShortNameFromUrl(std::string_view url) {
  if (url.empty())
    return "";

  std::string_view without_fragment = url;
  size_t hash = without_fragment.find('#');
  if (hash != std::string_view::npos)
    without_fragment = without_fragment.substr(0, hash);

  std::string_view without_query = without_fragment;
  size_t query = without_query.find('?');
  if (query != std::string_view::npos)
    without_query = without_query.substr(0, query);

  size_t scheme_end = without_query.find("://");
  std::string_view path_part =
      (scheme_end != std::string_view::npos)
          ? without_query.substr(scheme_end + 3)
          : without_query;

  while (path_part.size() > 1 && path_part.back() == '/')
    path_part.remove_suffix(1);

  size_t last_slash = path_part.rfind('/');
  if (last_slash != std::string_view::npos &&
      last_slash + 1 < path_part.size())
    return std::string(path_part.substr(last_slash + 1));

  return std::string(path_part.empty() ? url : path_part);
}

std::string StatusFromHandle(struct hlcache_handle* h) {
  if (h == nullptr)
    return "";
  const char* status_msg = content_get_status_message(h);
  if (status_msg != nullptr && status_msg[0] != '\0')
    return status_msg;

  switch (content_get_status(h)) {
    case CONTENT_STATUS_LOADING:
      return "Loading";
    case CONTENT_STATUS_READY:
      return "Ready";
    case CONTENT_STATUS_DONE:
      return "Done";
    case CONTENT_STATUS_ERROR:
      return "Error";
    default:
      return "Unknown";
  }
}

void PopulateResourceFromHandle(struct hlcache_handle* h, PageResource& res) {
  if (h == nullptr)
    return;

  res.handle = h;
  nsurl* url = hlcache_handle_get_url(h);
  if (url != nullptr && nsurl_access(url) != nullptr)
    res.url = nsurl_access(url);

  if (res.name.empty())
    res.name = DeriveShortNameFromUrl(res.url);

  lwc_string* mime = content_get_mime_type(h);
  if (mime != nullptr) {
    res.mime_type.assign(lwc_string_data(mime), lwc_string_length(mime));
    lwc_string_unref(mime);
  }

  size_t source_size = 0;
  const uint8_t* source_data = content_get_source_data(h, &source_size);
  if (source_data != nullptr && source_size > 0)
    res.size_bytes = source_size;

  res.status = StatusFromHandle(h);

  content_type type = content_get_type(h);
  if (type == CONTENT_IMAGE || res.category == ResourceCategory::kImage ||
      res.category == ResourceCategory::kBackgroundImage) {
    res.width = content_get_width(h);
    res.height = content_get_height(h);
  }
}

void CollectStylesheetAndImports(
    struct hlcache_handle* sheet_handle, bool is_internal,
    std::string_view fallback_name, std::vector<PageResource>& out,
    std::unordered_set<const void*>& seen_handles) {
  if (sheet_handle == nullptr)
    return;
  if (!seen_handles.insert(sheet_handle).second)
    return;

  PageResource res;
  res.category = ResourceCategory::kStylesheet;
  res.is_internal = is_internal;
  if (!fallback_name.empty())
    res.name = std::string(fallback_name);

  PopulateResourceFromHandle(sheet_handle, res);
  if (is_internal && res.status.empty())
    res.status = "Internal";

  out.push_back(std::move(res));

  if (content_get_type(sheet_handle) == CONTENT_CSS) {
    unsigned int import_count = 0;
    struct nscss_import* imports =
        nscss_get_imports(sheet_handle, &import_count);
    if (imports != nullptr) {
      for (unsigned int i = 0; i < import_count; ++i) {
        if (imports[i].c != nullptr)
          CollectStylesheetAndImports(imports[i].c, is_internal, "", out,
                                      seen_handles);
      }
    }
  }
}

void CollectFramesetUrls(const struct content_html_frames* frameset,
                         int count, std::vector<PageResource>& out,
                         std::unordered_set<std::string>& seen_frame_urls) {
  if (frameset == nullptr || count <= 0)
    return;

  for (int i = 0; i < count; ++i) {
    const struct content_html_frames& frame = frameset[i];
    if (frame.url != nullptr && nsurl_access(frame.url) != nullptr) {
      std::string url_str = nsurl_access(frame.url);
      if (seen_frame_urls.insert(url_str).second) {
        PageResource frame_res;
        frame_res.category = ResourceCategory::kFrame;
        frame_res.url = url_str;
        frame_res.name = frame.name != nullptr ? frame.name
                                               : DeriveShortNameFromUrl(url_str);
        frame_res.status = "Frame";
        out.push_back(std::move(frame_res));
      }
    }
    if (frame.children != nullptr && frame.rows * frame.cols > 0)
      CollectFramesetUrls(frame.children, frame.rows * frame.cols, out,
                          seen_frame_urls);
  }
}

void CollectFromBrowserWindow(struct browser_window* bw, bool is_frame,
                              std::vector<PageResource>& out,
                              std::unordered_set<const void*>& seen_handles,
                              std::unordered_set<std::string>& seen_frame_urls) {
  if (bw == nullptr)
    return;

  struct hlcache_handle* h = browser_window_get_content(bw);
  if (h != nullptr && seen_handles.insert(h).second) {
    PageResource doc_res;
    doc_res.category =
        is_frame ? ResourceCategory::kFrame : ResourceCategory::kDocument;
    PopulateResourceFromHandle(h, doc_res);
    if (doc_res.name.empty())
      doc_res.name = std::string(is_frame ? kFrameDocumentFallbackName
                                          : kMainDocumentFallbackName);
    if (!doc_res.url.empty())
      seen_frame_urls.insert(doc_res.url);
    std::string doc_url = doc_res.url;
    out.push_back(std::move(doc_res));

    if (content_get_type(h) == CONTENT_HTML) {
      unsigned int sheet_count = 0;
      struct html_stylesheet* sheets = html_get_stylesheets(h, &sheet_count);
      if (sheets != nullptr) {
        for (unsigned int i = 0; i < sheet_count; ++i) {
          if (sheets[i].sheet == nullptr)
            continue;
          bool internal = (i < STYLESHEET_START);
          std::string_view fallback_name;
          if (i == STYLESHEET_BASE)
            fallback_name = kBaseStylesheetName;
          else if (i == STYLESHEET_QUIRKS)
            fallback_name = kQuirksStylesheetName;
          else if (i == STYLESHEET_ADBLOCK)
            fallback_name = kAdblockStylesheetName;
          else if (i == STYLESHEET_USER)
            fallback_name = kUserStylesheetName;
          CollectStylesheetAndImports(sheets[i].sheet, internal, fallback_name,
                                      out, seen_handles);
        }
      }

      unsigned int script_count = 0;
      struct html_script* scripts = html_get_scripts(h, &script_count);
      if (scripts != nullptr) {
        unsigned int inline_index = 0;
        for (unsigned int i = 0; i < script_count; ++i) {
          if (scripts[i].type == html_script::HTML_SCRIPT_INLINE) {
            ++inline_index;
            if (scripts[i].data.string == nullptr)
              continue;
            const char* data_ptr = dom_string_data(scripts[i].data.string);
            size_t data_len = dom_string_byte_length(scripts[i].data.string);
            PageResource script_res;
            script_res.category = ResourceCategory::kScript;
            script_res.name =
                "(inline script #" + std::to_string(inline_index) + ")";
            script_res.url = doc_url;
            if (scripts[i].mimetype != nullptr &&
                dom_string_byte_length(scripts[i].mimetype) > 0) {
              script_res.mime_type.assign(
                  dom_string_data(scripts[i].mimetype),
                  dom_string_byte_length(scripts[i].mimetype));
            } else {
              script_res.mime_type = std::string(kDefaultScriptMimeType);
            }
            script_res.size_bytes = data_len;
            script_res.status = "Inline";
            if (data_ptr != nullptr && data_len > 0)
              script_res.inline_source.assign(data_ptr, data_len);
            out.push_back(std::move(script_res));
          } else if (scripts[i].data.handle != nullptr) {
            if (seen_handles.insert(scripts[i].data.handle).second) {
              PageResource script_res;
              script_res.category = ResourceCategory::kScript;
              PopulateResourceFromHandle(scripts[i].data.handle, script_res);
              out.push_back(std::move(script_res));
            }
          }
        }
      }

      unsigned int object_count = 0;
      struct content_html_object* objects = html_get_objects(h, &object_count);
      for (const struct content_html_object* obj = objects; obj != nullptr;
           obj = obj->next) {
        if (obj->content == nullptr)
          continue;
        if (!seen_handles.insert(obj->content).second)
          continue;

        PageResource obj_res;
        content_type obj_type = content_get_type(obj->content);
        if (obj->background)
          obj_res.category = ResourceCategory::kBackgroundImage;
        else if (obj_type == CONTENT_IMAGE)
          obj_res.category = ResourceCategory::kImage;
        else if (obj_type == CONTENT_HTML)
          obj_res.category = ResourceCategory::kFrame;
        else if (obj_type == CONTENT_CSS)
          obj_res.category = ResourceCategory::kStylesheet;
        else if (obj_type == CONTENT_JS)
          obj_res.category = ResourceCategory::kScript;
        else
          obj_res.category = ResourceCategory::kObject;

        PopulateResourceFromHandle(obj->content, obj_res);
        out.push_back(std::move(obj_res));
      }
    }
  }

  if (bw->children != nullptr) {
    int child_count = bw->rows * bw->cols;
    for (int i = 0; i < child_count; ++i) {
      CollectFromBrowserWindow(&bw->children[i], true, out, seen_handles,
                               seen_frame_urls);
    }
  }

  if (bw->iframes != nullptr) {
    for (int i = 0; i < bw->iframe_count; ++i) {
      CollectFromBrowserWindow(&bw->iframes[i], true, out, seen_handles,
                               seen_frame_urls);
    }
  }

  if (h != nullptr && content_get_type(h) == CONTENT_HTML) {
    struct content_html_frames* frameset = html_get_frameset(h);
    if (frameset != nullptr && frameset->children != nullptr)
      CollectFramesetUrls(frameset->children, frameset->rows * frameset->cols,
                          out, seen_frame_urls);

    for (const struct content_html_iframe* iframe = html_get_iframe(h);
         iframe != nullptr; iframe = iframe->next) {
      if (iframe->url != nullptr && nsurl_access(iframe->url) != nullptr) {
        std::string url_str = nsurl_access(iframe->url);
        if (seen_frame_urls.insert(url_str).second) {
          PageResource iframe_res;
          iframe_res.category = ResourceCategory::kFrame;
          iframe_res.url = url_str;
          iframe_res.name = iframe->name != nullptr
                                ? iframe->name
                                : DeriveShortNameFromUrl(url_str);
          iframe_res.status = "Frame";
          out.push_back(std::move(iframe_res));
        }
      }
    }
  }
}

}  // namespace

const char* ResourceCategoryToString(ResourceCategory category) {
  switch (category) {
    case ResourceCategory::kDocument:
      return "Document";
    case ResourceCategory::kStylesheet:
      return "Stylesheet";
    case ResourceCategory::kScript:
      return "Script";
    case ResourceCategory::kImage:
      return "Image";
    case ResourceCategory::kBackgroundImage:
      return "Background";
    case ResourceCategory::kObject:
      return "Object";
    case ResourceCategory::kFrame:
      return "Frame";
  }
  return "Resource";
}

std::vector<PageResource> CollectPageResources(struct browser_window* bw) {
  std::vector<PageResource> resources;
  if (bw == nullptr)
    return resources;

  std::unordered_set<const void*> seen_handles;
  std::unordered_set<std::string> seen_frame_urls;
  CollectFromBrowserWindow(bw, false, resources, seen_handles, seen_frame_urls);
  return resources;
}

}  // namespace netsurf::perception
