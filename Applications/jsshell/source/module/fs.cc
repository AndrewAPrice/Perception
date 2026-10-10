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

#include "module/fs.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "completion.h"
#include "perception/services.h"
#include "perception/storage_manager.h"

namespace module {
namespace {

// Maximum directory entries requested per StorageManager::ReadDirectory call.
constexpr uint64 kDirReadBatchSize = 256;

// Default indentation spaces used by fs.writeJson.
constexpr int32_t kDefaultJsonIndent = 2;

// Maximum recursion depth when walking directory trees.
constexpr int kMaxWalkDepth = 32;

// Maximum file size in bytes scanned by fs.grep to avoid binary/huge stalls.
constexpr uint64 kMaxGrepFileBytes = 4 * 1024 * 1024;

struct FsDirEntry {
  std::string name;
  std::string path;
  std::string type;
  uint64 size = 0;
  bool is_file = false;
  bool is_directory = false;
  bool is_symlink = false;
};

std::string JsToStdString(JSContext* ctx, JSValueConst val) {
  size_t len = 0;
  const char* cstr = JS_ToCStringLen(ctx, &len, val);
  if (!cstr)
    return {};
  std::string out(cstr, len);
  JS_FreeCString(ctx, cstr);
  return out;
}

JSValue MakeResolved(JSContext* ctx, JSValue val) {
  JSValue resolving_funcs[2];
  JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
  if (JS_IsException(promise)) {
    JS_FreeValue(ctx, val);
    return JS_EXCEPTION;
  }
  JSValue ret = JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &val);
  JS_FreeValue(ctx, ret);
  JS_FreeValue(ctx, val);
  JS_FreeValue(ctx, resolving_funcs[0]);
  JS_FreeValue(ctx, resolving_funcs[1]);
  return promise;
}

JSValue MakeRejected(JSContext* ctx, std::string_view message) {
  JSValue resolving_funcs[2];
  JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
  if (JS_IsException(promise))
    return JS_EXCEPTION;
  JSValue err = JS_NewError(ctx);
  JS_SetPropertyStr(
      ctx, err, "message",
      JS_NewStringLen(ctx, message.data(), message.size()));
  JSValue ret = JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, &err);
  JS_FreeValue(ctx, ret);
  JS_FreeValue(ctx, err);
  JS_FreeValue(ctx, resolving_funcs[0]);
  JS_FreeValue(ctx, resolving_funcs[1]);
  return promise;
}

std::string ResolveAgainstEngine(JSContext* ctx, std::string_view raw_path) {
  JsEngine* engine = GetJsEngine(ctx);
  std::string_view cwd = engine ? std::string_view(engine->Cwd()) : "/";
  return NormalizeLexicalPath(cwd, raw_path);
}

std::string JoinNormalizedChild(std::string_view dir, std::string_view name) {
  if (dir.empty() || dir == "/")
    return "/" + std::string(name);
  return std::string(dir) + "/" + std::string(name);
}

bool ListDirectoryEntries(std::string_view dir_path,
                          std::vector<FsDirEntry>& entries_out,
                          std::string& error_out) {
  auto storage = perception::GetService<perception::StorageManager>();
  auto stat_resp =
      storage.GetFileStatistics({std::string(dir_path), false});
  if (!stat_resp || !stat_resp->exists) {
    error_out = "Directory not found: " + std::string(dir_path);
    return false;
  }
  if (stat_resp->type != perception::DirectoryEntry::Type::DIRECTORY) {
    error_out = "Not a directory: " + std::string(dir_path);
    return false;
  }

  uint64 first_index = 0;
  while (true) {
    perception::ReadDirectoryRequest req;
    req.path = std::string(dir_path);
    req.first_index = first_index;
    req.maximum_number_of_entries = kDirReadBatchSize;
    auto resp = storage.ReadDirectory(req);
    if (!resp) {
      error_out = "Failed to read directory: " + std::string(dir_path);
      return false;
    }
    for (const auto& item : resp->entries) {
      FsDirEntry entry;
      entry.name = item.name;
      entry.path = JoinNormalizedChild(dir_path, item.name);
      entry.size = item.size_in_bytes;
      entry.is_symlink = item.is_link;
      entry.is_directory =
          (item.type == perception::DirectoryEntry::Type::DIRECTORY);
      entry.is_file =
          (item.type == perception::DirectoryEntry::Type::FILE) &&
          !item.is_link;
      if (item.is_link)
        entry.type = "link";
      else if (entry.is_directory)
        entry.type = "directory";
      else
        entry.type = "file";
      entries_out.push_back(std::move(entry));
    }
    if (!resp->has_more_entries || resp->entries.empty())
      break;
    first_index += resp->entries.size();
  }
  return true;
}

JSValue DirEntryToJsObject(JSContext* ctx, const FsDirEntry& entry) {
  JSValue obj = JS_NewObject(ctx);
  JS_SetPropertyStr(
      ctx, obj, "name",
      JS_NewStringLen(ctx, entry.name.data(), entry.name.size()));
  JS_SetPropertyStr(
      ctx, obj, "path",
      JS_NewStringLen(ctx, entry.path.data(), entry.path.size()));
  JS_SetPropertyStr(
      ctx, obj, "type",
      JS_NewStringLen(ctx, entry.type.data(), entry.type.size()));
  JS_SetPropertyStr(ctx, obj, "size",
                    JS_NewInt64(ctx, static_cast<int64_t>(entry.size)));
  JS_SetPropertyStr(ctx, obj, "isFile", JS_NewBool(ctx, entry.is_file));
  JS_SetPropertyStr(ctx, obj, "isDirectory",
                    JS_NewBool(ctx, entry.is_directory));
  JS_SetPropertyStr(ctx, obj, "isSymlink", JS_NewBool(ctx, entry.is_symlink));
  return obj;
}

bool ReadFileToStdString(std::string_view path, std::string& content_out,
                         std::string& error_out) {
  std::ifstream file{std::string(path), std::ios::binary};
  if (!file.is_open()) {
    error_out = "Cannot open file for reading: " + std::string(path);
    return false;
  }
  std::ostringstream ss;
  ss << file.rdbuf();
  content_out = ss.str();
  return true;
}

bool WriteStdStringToFile(std::string_view path, std::string_view content,
                          bool append, std::string& error_out) {
  std::ios::openmode mode = std::ios::binary | std::ios::out;
  if (append)
    mode |= std::ios::app;
  else
    mode |= std::ios::trunc;
  std::ofstream file{std::string(path), mode};
  if (!file.is_open()) {
    error_out = "Cannot open file for writing: " + std::string(path);
    return false;
  }
  if (!content.empty())
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!file.good()) {
    error_out = "Write error on file: " + std::string(path);
    return false;
  }
  return true;
}

bool ExtractBinaryArgument(JSContext* ctx, JSValueConst val,
                           std::string& bytes_out) {
  size_t byte_offset = 0;
  size_t byte_length = 0;
  size_t bytes_per_element = 0;
  JSValue ab = JS_GetTypedArrayBuffer(ctx, val, &byte_offset, &byte_length,
                                      &bytes_per_element);
  if (!JS_IsException(ab)) {
    size_t ab_size = 0;
    uint8_t* ptr = JS_GetArrayBuffer(ctx, &ab_size, ab);
    if (ptr && byte_offset + byte_length <= ab_size) {
      bytes_out.assign(reinterpret_cast<const char*>(ptr + byte_offset),
                       byte_length);
      JS_FreeValue(ctx, ab);
      return true;
    }
    JS_FreeValue(ctx, ab);
  } else {
    JS_FreeValue(ctx, JS_GetException(ctx));
  }

  size_t ab_size = 0;
  uint8_t* ptr = JS_GetArrayBuffer(ctx, &ab_size, val);
  if (ptr) {
    bytes_out.assign(reinterpret_cast<const char*>(ptr), ab_size);
    return true;
  }
  JS_FreeValue(ctx, JS_GetException(ctx));
  return false;
}

bool GetOptionBool(JSContext* ctx, int argc, JSValueConst* argv, int opt_idx,
                   const char* prop_name, bool default_val = false) {
  if (argc <= opt_idx || !JS_IsObject(argv[opt_idx]))
    return default_val;
  JSValue prop = JS_GetPropertyStr(ctx, argv[opt_idx], prop_name);
  if (JS_IsUndefined(prop)) {
    JS_FreeValue(ctx, prop);
    return default_val;
  }
  bool res = JS_ToBool(ctx, prop) > 0;
  JS_FreeValue(ctx, prop);
  return res;
}

bool CreateDirectoryTree(std::string_view normalized_path, bool recursive,
                         std::string& error_out) {
  auto storage = perception::GetService<perception::StorageManager>();
  if (!recursive) {
    Status status =
        storage.CreateDirectory({std::string(normalized_path), false});
    if (status != Status::OK) {
      error_out = "Failed to create directory: " + std::string(normalized_path);
      return false;
    }
    return true;
  }

  std::string current;
  size_t pos = 1;
  while (pos <= normalized_path.size()) {
    size_t slash = normalized_path.find('/', pos);
    size_t end =
        (slash == std::string_view::npos) ? normalized_path.size() : slash;
    std::string prefix(normalized_path.substr(0, end));
    if (!prefix.empty() && prefix != "/") {
      auto stat_resp = storage.GetFileStatistics({prefix, false});
      if (!stat_resp || !stat_resp->exists) {
        Status st = storage.CreateDirectory({prefix, false});
        if (st != Status::OK) {
          error_out = "Failed to create directory: " + prefix;
          return false;
        }
      }
    }
    if (slash == std::string_view::npos)
      break;
    pos = slash + 1;
  }
  return true;
}

bool CopyRecursiveImpl(std::string_view src_path, std::string_view dst_path,
                       bool recursive, int depth, std::string& error_out) {
  if (depth > kMaxWalkDepth) {
    error_out = "Maximum directory recursion depth exceeded";
    return false;
  }
  auto storage = perception::GetService<perception::StorageManager>();
  auto stat_resp =
      storage.GetFileStatistics({std::string(src_path), false});
  if (!stat_resp || !stat_resp->exists) {
    error_out = "Source path does not exist: " + std::string(src_path);
    return false;
  }

  if (stat_resp->type == perception::DirectoryEntry::Type::DIRECTORY) {
    if (!recursive) {
      error_out = "Cannot copy directory without { recursive: true }: " +
                  std::string(src_path);
      return false;
    }
    if (!CreateDirectoryTree(dst_path, true, error_out))
      return false;
    std::vector<FsDirEntry> children;
    if (!ListDirectoryEntries(src_path, children, error_out))
      return false;
    for (const auto& child : children) {
      std::string child_dst = JoinNormalizedChild(dst_path, child.name);
      if (!CopyRecursiveImpl(child.path, child_dst, true, depth + 1,
                             error_out)) {
        return false;
      }
    }
    return true;
  }

  std::string data;
  if (!ReadFileToStdString(src_path, data, error_out))
    return false;
  return WriteStdStringToFile(dst_path, data, false, error_out);
}

bool RemoveRecursiveImpl(std::string_view path, bool recursive, int depth,
                         std::string& error_out) {
  if (depth > kMaxWalkDepth) {
    error_out = "Maximum directory recursion depth exceeded";
    return false;
  }
  auto storage = perception::GetService<perception::StorageManager>();
  auto stat_resp = storage.GetFileStatistics({std::string(path), false});
  if (!stat_resp || !stat_resp->exists) {
    error_out = "Path does not exist: " + std::string(path);
    return false;
  }

  if (stat_resp->type == perception::DirectoryEntry::Type::DIRECTORY &&
      recursive) {
    std::vector<FsDirEntry> children;
    if (ListDirectoryEntries(path, children, error_out)) {
      for (const auto& child : children) {
        if (!RemoveRecursiveImpl(child.path, true, depth + 1, error_out))
          return false;
      }
    }
  }

  Status status = storage.DeleteFileOrDirectory({std::string(path), false});
  if (status != Status::OK) {
    error_out = "Failed to remove: " + std::string(path);
    return false;
  }
  return true;
}

void WalkDirectoryRecursive(std::string_view dir_path, int depth,
                            std::vector<FsDirEntry>& out) {
  if (depth > kMaxWalkDepth)
    return;
  std::vector<FsDirEntry> children;
  std::string unused_err;
  if (!ListDirectoryEntries(dir_path, children, unused_err))
    return;
  for (const auto& child : children) {
    out.push_back(child);
    if (child.is_directory && !child.is_symlink)
      WalkDirectoryRecursive(child.path, depth + 1, out);
  }
}

bool ContainsSubstringCaseInsensitive(std::string_view text,
                                      std::string_view pattern,
                                      bool ignore_case) {
  if (pattern.empty())
    return true;
  if (!ignore_case)
    return text.find(pattern) != std::string_view::npos;
  auto it = std::search(
      text.begin(), text.end(), pattern.begin(), pattern.end(),
      [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
      });
  return it != text.end();
}

JSValue JsFsCwd(JSContext* ctx, JSValueConst this_val, int argc,
                JSValueConst* argv) {
  (void)this_val;
  (void)argc;
  (void)argv;
  JsEngine* engine = GetJsEngine(ctx);
  const std::string& cwd = engine ? engine->Cwd() : "/";
  return JS_NewStringLen(ctx, cwd.data(), cwd.size());
}

JSValue JsFsChdir(JSContext* ctx, JSValueConst this_val, int argc,
                  JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "fs.chdir() requires a path argument");
  JsEngine* engine = GetJsEngine(ctx);
  if (!engine)
    return JS_ThrowInternalError(ctx, "Missing JsEngine instance");
  std::string path = JsToStdString(ctx, argv[0]);
  std::string err;
  if (!engine->SetCwd(path, err))
    return JS_ThrowInternalError(ctx, "%s", err.c_str());
  return JS_UNDEFINED;
}

JSValue JsFsReadDir(JSContext* ctx, JSValueConst this_val, int argc,
                    JSValueConst* argv) {
  (void)this_val;
  std::string raw_path =
      (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]))
          ? JsToStdString(ctx, argv[0])
          : ".";
  std::string resolved = ResolveAgainstEngine(ctx, raw_path);
  std::vector<FsDirEntry> entries;
  std::string err;
  if (!ListDirectoryEntries(resolved, entries, err))
    return MakeRejected(ctx, err);

  JSValue arr = JS_NewArray(ctx);
  for (uint32_t i = 0; i < entries.size(); ++i)
    JS_SetPropertyUint32(ctx, arr, i, DirEntryToJsObject(ctx, entries[i]));
  return MakeResolved(ctx, arr);
}

JSValue JsFsStat(JSContext* ctx, JSValueConst this_val, int argc,
                 JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return MakeRejected(ctx, "fs.stat() requires a path argument");
  std::string resolved =
      ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[0]));

  auto storage = perception::GetService<perception::StorageManager>();
  auto stat_resp = storage.GetFileStatistics({resolved, false});
  if (!stat_resp || !stat_resp->exists)
    return MakeRejected(ctx, "File not found: " + resolved);
  auto perm_resp = storage.CheckPermissions({resolved, false});

  bool is_dir =
      (stat_resp->type == perception::DirectoryEntry::Type::DIRECTORY);
  bool is_link = stat_resp->is_link;
  bool is_file =
      (stat_resp->type == perception::DirectoryEntry::Type::FILE) && !is_link;
  std::string type_str =
      is_link ? "link" : (is_dir ? "directory" : "file");

  JSValue obj = JS_NewObject(ctx);
  JS_SetPropertyStr(
      ctx, obj, "path",
      JS_NewStringLen(ctx, resolved.data(), resolved.size()));
  JS_SetPropertyStr(
      ctx, obj, "type",
      JS_NewStringLen(ctx, type_str.data(), type_str.size()));
  JS_SetPropertyStr(
      ctx, obj, "size",
      JS_NewInt64(ctx, static_cast<int64_t>(stat_resp->size_in_bytes)));
  JS_SetPropertyStr(ctx, obj, "isReadable",
                    JS_NewBool(ctx, perm_resp ? perm_resp->can_read : true));
  JS_SetPropertyStr(ctx, obj, "isWritable",
                    JS_NewBool(ctx, perm_resp ? perm_resp->can_write : false));
  JS_SetPropertyStr(
      ctx, obj, "isExecutable",
      JS_NewBool(ctx, perm_resp ? perm_resp->can_execute : false));
  JS_SetPropertyStr(ctx, obj, "isFile", JS_NewBool(ctx, is_file));
  JS_SetPropertyStr(ctx, obj, "isDirectory", JS_NewBool(ctx, is_dir));
  JS_SetPropertyStr(ctx, obj, "isSymlink", JS_NewBool(ctx, is_link));
  return MakeResolved(ctx, obj);
}

JSValue JsFsExists(JSContext* ctx, JSValueConst this_val, int argc,
                   JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return MakeResolved(ctx, JS_NewBool(ctx, false));
  std::string resolved =
      ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[0]));
  auto stat_resp =
      perception::GetService<perception::StorageManager>().GetFileStatistics(
          {resolved, false});
  bool exists = stat_resp && stat_resp->exists;
  return MakeResolved(ctx, JS_NewBool(ctx, exists));
}

JSValue JsFsReadTextFile(JSContext* ctx, JSValueConst this_val, int argc,
                         JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return MakeRejected(ctx, "fs.readTextFile() requires a path argument");
  std::string resolved =
      ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[0]));
  std::string content;
  std::string err;
  if (!ReadFileToStdString(resolved, content, err))
    return MakeRejected(ctx, err);
  return MakeResolved(ctx,
                      JS_NewStringLen(ctx, content.data(), content.size()));
}

JSValue JsFsReadFile(JSContext* ctx, JSValueConst this_val, int argc,
                     JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return MakeRejected(ctx, "fs.readFile() requires a path argument");
  std::string resolved =
      ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[0]));
  std::string content;
  std::string err;
  if (!ReadFileToStdString(resolved, content, err))
    return MakeRejected(ctx, err);
  JSValue ab = JS_NewArrayBufferCopy(
      ctx, reinterpret_cast<const uint8_t*>(content.data()), content.size());
  if (JS_IsException(ab))
    return JS_EXCEPTION;
  JSValue u8 = JS_NewTypedArray(ctx, 1, &ab, JS_TYPED_ARRAY_UINT8);
  JS_FreeValue(ctx, ab);
  if (JS_IsException(u8))
    return JS_EXCEPTION;
  return MakeResolved(ctx, u8);
}

JSValue JsFsReadLines(JSContext* ctx, JSValueConst this_val, int argc,
                      JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return MakeRejected(ctx, "fs.readLines() requires a path argument");
  std::string resolved =
      ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[0]));
  std::string content;
  std::string err;
  if (!ReadFileToStdString(resolved, content, err))
    return MakeRejected(ctx, err);

  JSValue arr = JS_NewArray(ctx);
  uint32_t idx = 0;
  size_t pos = 0;
  while (pos < content.size()) {
    size_t nl = content.find('\n', pos);
    size_t end = (nl == std::string::npos) ? content.size() : nl;
    size_t len = end - pos;
    if (len > 0 && content[pos + len - 1] == '\r')
      --len;
    JS_SetPropertyUint32(
        ctx, arr, idx++, JS_NewStringLen(ctx, content.data() + pos, len));
    if (nl == std::string::npos)
      break;
    pos = nl + 1;
  }
  return MakeResolved(ctx, arr);
}

JSValue JsFsReadJson(JSContext* ctx, JSValueConst this_val, int argc,
                     JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return MakeRejected(ctx, "fs.readJson() requires a path argument");
  std::string resolved =
      ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[0]));
  std::string content;
  std::string err;
  if (!ReadFileToStdString(resolved, content, err))
    return MakeRejected(ctx, err);
  JSValue parsed =
      JS_ParseJSON(ctx, content.c_str(), content.size(), resolved.c_str());
  if (JS_IsException(parsed)) {
    JSValue exc = JS_GetException(ctx);
    std::string msg = JsToStdString(ctx, exc);
    JS_FreeValue(ctx, exc);
    return MakeRejected(ctx, msg);
  }
  return MakeResolved(ctx, parsed);
}

JSValue JsFsWriteTextFile(JSContext* ctx, JSValueConst this_val, int argc,
                          JSValueConst* argv) {
  (void)this_val;
  if (argc < 2)
    return MakeRejected(ctx, "fs.writeTextFile() requires path and data");
  std::string resolved =
      ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[0]));
  std::string data = JsToStdString(ctx, argv[1]);
  bool append = GetOptionBool(ctx, argc, argv, 2, "append", false);
  std::string err;
  if (!WriteStdStringToFile(resolved, data, append, err))
    return MakeRejected(ctx, err);
  return MakeResolved(ctx, JS_UNDEFINED);
}

JSValue JsFsWriteFile(JSContext* ctx, JSValueConst this_val, int argc,
                      JSValueConst* argv) {
  (void)this_val;
  if (argc < 2)
    return MakeRejected(ctx, "fs.writeFile() requires path and data");
  std::string resolved =
      ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[0]));
  std::string bytes;
  if (!ExtractBinaryArgument(ctx, argv[1], bytes))
    bytes = JsToStdString(ctx, argv[1]);
  bool append = GetOptionBool(ctx, argc, argv, 2, "append", false);
  std::string err;
  if (!WriteStdStringToFile(resolved, bytes, append, err))
    return MakeRejected(ctx, err);
  return MakeResolved(ctx, JS_UNDEFINED);
}

JSValue JsFsWriteJson(JSContext* ctx, JSValueConst this_val, int argc,
                      JSValueConst* argv) {
  (void)this_val;
  if (argc < 2)
    return MakeRejected(ctx, "fs.writeJson() requires path and value");
  std::string resolved =
      ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[0]));
  int32_t indent = kDefaultJsonIndent;
  if (argc >= 3 && JS_IsObject(argv[2])) {
    JSValue indent_val = JS_GetPropertyStr(ctx, argv[2], "indent");
    if (JS_IsNumber(indent_val))
      JS_ToInt32(ctx, &indent, indent_val);
    JS_FreeValue(ctx, indent_val);
  }
  JSValue space = JS_NewInt32(ctx, indent);
  JSValue json_str = JS_JSONStringify(ctx, argv[1], JS_UNDEFINED, space);
  JS_FreeValue(ctx, space);
  if (JS_IsException(json_str)) {
    JSValue exc = JS_GetException(ctx);
    std::string msg = JsToStdString(ctx, exc);
    JS_FreeValue(ctx, exc);
    return MakeRejected(ctx, msg);
  }
  std::string serialized = JsToStdString(ctx, json_str);
  JS_FreeValue(ctx, json_str);
  serialized.push_back('\n');

  std::string err;
  if (!WriteStdStringToFile(resolved, serialized, false, err))
    return MakeRejected(ctx, err);
  return MakeResolved(ctx, JS_UNDEFINED);
}

JSValue JsFsCopy(JSContext* ctx, JSValueConst this_val, int argc,
                 JSValueConst* argv) {
  (void)this_val;
  if (argc < 2)
    return MakeRejected(ctx, "fs.copy() requires src and dst paths");
  std::string src = ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[0]));
  std::string dst = ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[1]));
  bool recursive = GetOptionBool(ctx, argc, argv, 2, "recursive", true);
  std::string err;
  if (!CopyRecursiveImpl(src, dst, recursive, 0, err))
    return MakeRejected(ctx, err);
  return MakeResolved(ctx, JS_UNDEFINED);
}

JSValue JsFsMove(JSContext* ctx, JSValueConst this_val, int argc,
                 JSValueConst* argv) {
  (void)this_val;
  if (argc < 2)
    return MakeRejected(ctx, "fs.move() requires src and dst paths");
  std::string src = ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[0]));
  std::string dst = ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[1]));
  if (src == dst)
    return MakeResolved(ctx, JS_UNDEFINED);
  std::string err;
  if (!CopyRecursiveImpl(src, dst, true, 0, err))
    return MakeRejected(ctx, err);
  if (!RemoveRecursiveImpl(src, true, 0, err))
    return MakeRejected(ctx, err);
  return MakeResolved(ctx, JS_UNDEFINED);
}

JSValue JsFsRemove(JSContext* ctx, JSValueConst this_val, int argc,
                   JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return MakeRejected(ctx, "fs.remove() requires a path argument");
  std::string path = ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[0]));
  bool recursive = GetOptionBool(ctx, argc, argv, 1, "recursive", false);
  std::string err;
  if (!RemoveRecursiveImpl(path, recursive, 0, err))
    return MakeRejected(ctx, err);
  return MakeResolved(ctx, JS_UNDEFINED);
}

JSValue JsFsMkdir(JSContext* ctx, JSValueConst this_val, int argc,
                  JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return MakeRejected(ctx, "fs.mkdir() requires a path argument");
  std::string path = ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[0]));
  bool recursive = GetOptionBool(ctx, argc, argv, 1, "recursive", false);
  std::string err;
  if (!CreateDirectoryTree(path, recursive, err))
    return MakeRejected(ctx, err);
  return MakeResolved(ctx, JS_UNDEFINED);
}

JSValue JsFsReadLink(JSContext* ctx, JSValueConst this_val, int argc,
                     JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return MakeRejected(ctx, "fs.readLink() requires a path argument");
  std::string path = ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[0]));
  auto resp =
      perception::GetService<perception::StorageManager>().ReadLink(
          {path, true});
  if (!resp)
    return MakeRejected(ctx, "Failed to read link: " + path);
  return MakeResolved(
      ctx, JS_NewStringLen(ctx, resp->path.data(), resp->path.size()));
}

JSValue JsFsGlob(JSContext* ctx, JSValueConst this_val, int argc,
                 JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return MakeRejected(ctx, "fs.glob() requires a pattern argument");
  std::string raw_pattern = JsToStdString(ctx, argv[0]);
  std::string base_dir =
      (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
          ? ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[1]))
          : ResolveAgainstEngine(ctx, ".");

  std::string full_pattern =
      raw_pattern.starts_with("/")
          ? NormalizeLexicalPath("/", raw_pattern)
          : JoinNormalizedChild(base_dir, raw_pattern);

  std::string walk_root = base_dir;
  if (raw_pattern.starts_with("/")) {
    size_t first_wild = raw_pattern.find_first_of("*?");
    if (first_wild == std::string::npos) {
      walk_root = NormalizeLexicalPath("/", raw_pattern);
      size_t last_slash = walk_root.rfind('/');
      walk_root = (last_slash == 0 || last_slash == std::string::npos)
                      ? "/"
                      : walk_root.substr(0, last_slash);
    } else {
      size_t slash_before = raw_pattern.rfind('/', first_wild);
      walk_root = (slash_before == 0 || slash_before == std::string::npos)
                      ? "/"
                      : NormalizeLexicalPath(
                            "/", raw_pattern.substr(0, slash_before));
    }
  }

  std::vector<FsDirEntry> all_entries;
  WalkDirectoryRecursive(walk_root, 0, all_entries);

  std::vector<std::string> matches;
  for (const auto& entry : all_entries) {
    if (MatchesGlobPattern(full_pattern, entry.path) ||
        MatchesGlobPattern(raw_pattern, entry.name)) {
      matches.push_back(entry.path);
    }
  }
  std::sort(matches.begin(), matches.end());

  JSValue arr = JS_NewArray(ctx);
  for (uint32_t i = 0; i < matches.size(); ++i) {
    JS_SetPropertyUint32(
        ctx, arr, i,
        JS_NewStringLen(ctx, matches[i].data(), matches[i].size()));
  }
  return MakeResolved(ctx, arr);
}

JSValue JsFsFind(JSContext* ctx, JSValueConst this_val, int argc,
                 JSValueConst* argv) {
  (void)this_val;
  std::string root =
      (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]))
          ? ResolveAgainstEngine(ctx, JsToStdString(ctx, argv[0]))
          : ResolveAgainstEngine(ctx, ".");

  std::vector<FsDirEntry> entries;
  WalkDirectoryRecursive(root, 0, entries);

  bool has_filter =
      (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]));
  bool is_fn_filter = has_filter && JS_IsFunction(ctx, argv[1]);
  JSValue test_fn = JS_UNDEFINED;
  bool is_regex_filter = false;
  std::string str_filter;
  if (has_filter && !is_fn_filter && JS_IsObject(argv[1])) {
    test_fn = JS_GetPropertyStr(ctx, argv[1], "test");
    if (JS_IsFunction(ctx, test_fn))
      is_regex_filter = true;
    else
      JS_FreeValue(ctx, test_fn);
  } else if (has_filter && !is_fn_filter) {
    str_filter = JsToStdString(ctx, argv[1]);
  }

  JSValue arr = JS_NewArray(ctx);
  uint32_t out_idx = 0;
  for (const auto& entry : entries) {
    JSValue entry_obj = DirEntryToJsObject(ctx, entry);
    bool keep = true;
    if (is_fn_filter) {
      JSValue res = JS_Call(ctx, argv[1], JS_UNDEFINED, 1, &entry_obj);
      if (JS_IsException(res)) {
        JS_FreeValue(ctx, entry_obj);
        JS_FreeValue(ctx, arr);
        return JS_EXCEPTION;
      }
      keep = JS_ToBool(ctx, res) > 0;
      JS_FreeValue(ctx, res);
    } else if (is_regex_filter) {
      JSValue path_str =
          JS_NewStringLen(ctx, entry.path.data(), entry.path.size());
      JSValue res = JS_Call(ctx, test_fn, argv[1], 1, &path_str);
      JS_FreeValue(ctx, path_str);
      keep = !JS_IsException(res) && (JS_ToBool(ctx, res) > 0);
      JS_FreeValue(ctx, res);
    } else if (!str_filter.empty()) {
      if (str_filter.find_first_of("*?") != std::string::npos) {
        keep = MatchesGlobPattern(str_filter, entry.name) ||
               MatchesGlobPattern(str_filter, entry.path);
      } else {
        keep = ContainsSubstringCaseInsensitive(entry.name, str_filter, true) ||
               ContainsSubstringCaseInsensitive(entry.path, str_filter, true);
      }
    }

    if (keep)
      JS_SetPropertyUint32(ctx, arr, out_idx++, entry_obj);
    else
      JS_FreeValue(ctx, entry_obj);
  }

  if (is_regex_filter)
    JS_FreeValue(ctx, test_fn);
  return MakeResolved(ctx, arr);
}

JSValue JsFsGrep(JSContext* ctx, JSValueConst this_val, int argc,
                 JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return MakeRejected(ctx, "fs.grep() requires a pattern argument");

  bool is_regex = false;
  JSValue exec_fn = JS_UNDEFINED;
  std::string needle;
  if (JS_IsObject(argv[0])) {
    exec_fn = JS_GetPropertyStr(ctx, argv[0], "exec");
    if (JS_IsFunction(ctx, exec_fn))
      is_regex = true;
    else
      JS_FreeValue(ctx, exec_fn);
  }
  if (!is_regex)
    needle = JsToStdString(ctx, argv[0]);

  std::string raw_target =
      (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
          ? JsToStdString(ctx, argv[1])
          : ".";
  bool ignore_case = GetOptionBool(ctx, argc, argv, 2, "ignoreCase", false);

  std::vector<std::string> candidate_files;
  std::string resolved_target = ResolveAgainstEngine(ctx, raw_target);
  auto storage = perception::GetService<perception::StorageManager>();
  auto stat_resp = storage.GetFileStatistics({resolved_target, false});

  if (stat_resp && stat_resp->exists) {
    if (stat_resp->type == perception::DirectoryEntry::Type::FILE) {
      candidate_files.push_back(resolved_target);
    } else {
      std::vector<FsDirEntry> walked;
      WalkDirectoryRecursive(resolved_target, 0, walked);
      for (const auto& e : walked) {
        if (e.is_file && e.size <= kMaxGrepFileBytes)
          candidate_files.push_back(e.path);
      }
    }
  } else if (raw_target.find_first_of("*?") != std::string::npos) {
    std::string walk_root = ResolveAgainstEngine(ctx, ".");
    std::vector<FsDirEntry> walked;
    WalkDirectoryRecursive(walk_root, 0, walked);
    for (const auto& e : walked) {
      if (e.is_file && e.size <= kMaxGrepFileBytes &&
          (MatchesGlobPattern(resolved_target, e.path) ||
           MatchesGlobPattern(raw_target, e.name))) {
        candidate_files.push_back(e.path);
      }
    }
  } else {
    if (is_regex)
      JS_FreeValue(ctx, exec_fn);
    return MakeRejected(ctx, "Path not found: " + resolved_target);
  }

  JSValue results = JS_NewArray(ctx);
  uint32_t match_idx = 0;

  for (const auto& file_path : candidate_files) {
    std::string content;
    std::string unused_err;
    if (!ReadFileToStdString(file_path, content, unused_err))
      continue;
    if (content.find('\0') != std::string::npos)
      continue;

    size_t pos = 0;
    int line_number = 1;
    while (pos <= content.size()) {
      size_t nl = content.find('\n', pos);
      size_t end = (nl == std::string::npos) ? content.size() : nl;
      size_t len = end - pos;
      if (len > 0 && content[pos + len - 1] == '\r')
        --len;
      std::string_view line_sv(content.data() + pos, len);

      bool matched = false;
      int col_1based = 1;
      if (is_regex) {
        JS_SetPropertyStr(ctx, argv[0], "lastIndex", JS_NewInt32(ctx, 0));
        JSValue line_val =
            JS_NewStringLen(ctx, line_sv.data(), line_sv.size());
        JSValue m = JS_Call(ctx, exec_fn, argv[0], 1, &line_val);
        JS_FreeValue(ctx, line_val);
        if (!JS_IsException(m) && !JS_IsNull(m) && JS_IsObject(m)) {
          matched = true;
          JSValue idx_val = JS_GetPropertyStr(ctx, m, "index");
          int32_t zero_idx = 0;
          if (JS_ToInt32(ctx, &zero_idx, idx_val) == 0)
            col_1based = zero_idx + 1;
          JS_FreeValue(ctx, idx_val);
        }
        JS_FreeValue(ctx, m);
      } else if (!needle.empty()) {
        if (!ignore_case) {
          size_t found = line_sv.find(needle);
          if (found != std::string_view::npos) {
            matched = true;
            col_1based = static_cast<int>(found) + 1;
          }
        } else {
          auto it = std::search(
              line_sv.begin(), line_sv.end(), needle.begin(), needle.end(),
              [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) ==
                       std::tolower(static_cast<unsigned char>(b));
              });
          if (it != line_sv.end()) {
            matched = true;
            col_1based =
                static_cast<int>(std::distance(line_sv.begin(), it)) + 1;
          }
        }
      }

      if (matched) {
        JSValue item = JS_NewObject(ctx);
        JS_SetPropertyStr(
            ctx, item, "path",
            JS_NewStringLen(ctx, file_path.data(), file_path.size()));
        JS_SetPropertyStr(ctx, item, "line", JS_NewInt32(ctx, line_number));
        JS_SetPropertyStr(ctx, item, "column", JS_NewInt32(ctx, col_1based));
        JS_SetPropertyStr(
            ctx, item, "text",
            JS_NewStringLen(ctx, line_sv.data(), line_sv.size()));
        JS_SetPropertyUint32(ctx, results, match_idx++, item);
      }

      if (nl == std::string::npos)
        break;
      pos = nl + 1;
      ++line_number;
    }
  }

  if (is_regex)
    JS_FreeValue(ctx, exec_fn);
  return MakeResolved(ctx, results);
}

JSValue JsFsPathJoin(JSContext* ctx, JSValueConst this_val, int argc,
                     JSValueConst* argv) {
  (void)this_val;
  if (argc == 0)
    return JS_NewString(ctx, ".");
  std::string combined;
  for (int i = 0; i < argc; ++i) {
    std::string part = JsToStdString(ctx, argv[i]);
    if (part.empty())
      continue;
    if (!combined.empty() && combined.back() != '/' && part.front() != '/')
      combined.push_back('/');
    combined += part;
  }
  if (combined.empty())
    return JS_NewString(ctx, ".");
  if (combined.front() == '/') {
    std::string norm = NormalizeLexicalPath("/", combined);
    return JS_NewStringLen(ctx, norm.data(), norm.size());
  }
  std::string norm = NormalizeLexicalPath("/", combined);
  if (norm.size() > 1 && norm.front() == '/')
    norm.erase(0, 1);
  return JS_NewStringLen(ctx, norm.data(), norm.size());
}

JSValue JsFsPathResolve(JSContext* ctx, JSValueConst this_val, int argc,
                        JSValueConst* argv) {
  (void)this_val;
  JsEngine* engine = GetJsEngine(ctx);
  std::string current = engine ? engine->Cwd() : "/";
  for (int i = 0; i < argc; ++i) {
    std::string part = JsToStdString(ctx, argv[i]);
    if (!part.empty())
      current = NormalizeLexicalPath(current, part);
  }
  return JS_NewStringLen(ctx, current.data(), current.size());
}

JSValue JsFsPathNormalize(JSContext* ctx, JSValueConst this_val, int argc,
                          JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return JS_NewString(ctx, ".");
  std::string raw = JsToStdString(ctx, argv[0]);
  if (raw.empty())
    return JS_NewString(ctx, ".");
  std::string norm = NormalizeLexicalPath("/", raw);
  if (raw.front() != '/' && norm.size() > 1 && norm.front() == '/')
    norm.erase(0, 1);
  return JS_NewStringLen(ctx, norm.data(), norm.size());
}

JSValue JsFsPathDirname(JSContext* ctx, JSValueConst this_val, int argc,
                        JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return JS_NewString(ctx, ".");
  std::string p = JsToStdString(ctx, argv[0]);
  while (p.size() > 1 && p.back() == '/')
    p.pop_back();
  size_t slash = p.rfind('/');
  if (slash == std::string::npos)
    return JS_NewString(ctx, ".");
  if (slash == 0)
    return JS_NewString(ctx, "/");
  std::string dir = p.substr(0, slash);
  return JS_NewStringLen(ctx, dir.data(), dir.size());
}

JSValue JsFsPathBasename(JSContext* ctx, JSValueConst this_val, int argc,
                         JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return JS_NewString(ctx, "");
  std::string p = JsToStdString(ctx, argv[0]);
  while (p.size() > 1 && p.back() == '/')
    p.pop_back();
  size_t slash = p.rfind('/');
  std::string base =
      (slash == std::string::npos) ? p : p.substr(slash + 1);
  if (argc >= 2 && JS_IsString(argv[1])) {
    std::string ext = JsToStdString(ctx, argv[1]);
    if (!ext.empty() && base.size() >= ext.size() &&
        base.compare(base.size() - ext.size(), ext.size(), ext) == 0) {
      base.resize(base.size() - ext.size());
    }
  }
  return JS_NewStringLen(ctx, base.data(), base.size());
}

JSValue JsFsPathExtname(JSContext* ctx, JSValueConst this_val, int argc,
                        JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return JS_NewString(ctx, "");
  std::string p = JsToStdString(ctx, argv[0]);
  while (p.size() > 1 && p.back() == '/')
    p.pop_back();
  size_t slash = p.rfind('/');
  std::string base =
      (slash == std::string::npos) ? p : p.substr(slash + 1);
  size_t dot = base.rfind('.');
  if (dot == std::string::npos || dot == 0)
    return JS_NewString(ctx, "");
  std::string ext = base.substr(dot);
  return JS_NewStringLen(ctx, ext.data(), ext.size());
}

}  // namespace

void RegisterFsModule(JSContext* ctx, JsEngine& engine) {
  (void)engine;
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue fs_obj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, fs_obj, "cwd",
                    JS_NewCFunction(ctx, JsFsCwd, "cwd", 0));
  JS_SetPropertyStr(ctx, fs_obj, "chdir",
                    JS_NewCFunction(ctx, JsFsChdir, "chdir", 1));
  JS_SetPropertyStr(ctx, fs_obj, "readDir",
                    JS_NewCFunction(ctx, JsFsReadDir, "readDir", 1));
  JS_SetPropertyStr(ctx, fs_obj, "stat",
                    JS_NewCFunction(ctx, JsFsStat, "stat", 1));
  JS_SetPropertyStr(ctx, fs_obj, "exists",
                    JS_NewCFunction(ctx, JsFsExists, "exists", 1));
  JS_SetPropertyStr(ctx, fs_obj, "readTextFile",
                    JS_NewCFunction(ctx, JsFsReadTextFile, "readTextFile", 1));
  JS_SetPropertyStr(ctx, fs_obj, "readFile",
                    JS_NewCFunction(ctx, JsFsReadFile, "readFile", 1));
  JS_SetPropertyStr(ctx, fs_obj, "readLines",
                    JS_NewCFunction(ctx, JsFsReadLines, "readLines", 1));
  JS_SetPropertyStr(ctx, fs_obj, "readJson",
                    JS_NewCFunction(ctx, JsFsReadJson, "readJson", 1));
  JS_SetPropertyStr(
      ctx, fs_obj, "writeTextFile",
      JS_NewCFunction(ctx, JsFsWriteTextFile, "writeTextFile", 3));
  JS_SetPropertyStr(ctx, fs_obj, "writeFile",
                    JS_NewCFunction(ctx, JsFsWriteFile, "writeFile", 3));
  JS_SetPropertyStr(ctx, fs_obj, "writeJson",
                    JS_NewCFunction(ctx, JsFsWriteJson, "writeJson", 3));
  JS_SetPropertyStr(ctx, fs_obj, "copy",
                    JS_NewCFunction(ctx, JsFsCopy, "copy", 3));
  JS_SetPropertyStr(ctx, fs_obj, "move",
                    JS_NewCFunction(ctx, JsFsMove, "move", 2));
  JS_SetPropertyStr(ctx, fs_obj, "remove",
                    JS_NewCFunction(ctx, JsFsRemove, "remove", 2));
  JS_SetPropertyStr(ctx, fs_obj, "mkdir",
                    JS_NewCFunction(ctx, JsFsMkdir, "mkdir", 2));
  JS_SetPropertyStr(ctx, fs_obj, "readLink",
                    JS_NewCFunction(ctx, JsFsReadLink, "readLink", 1));
  JS_SetPropertyStr(ctx, fs_obj, "glob",
                    JS_NewCFunction(ctx, JsFsGlob, "glob", 2));
  JS_SetPropertyStr(ctx, fs_obj, "find",
                    JS_NewCFunction(ctx, JsFsFind, "find", 2));
  JS_SetPropertyStr(ctx, fs_obj, "grep",
                    JS_NewCFunction(ctx, JsFsGrep, "grep", 3));

  JSValue path_obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, path_obj, "join",
                    JS_NewCFunction(ctx, JsFsPathJoin, "join", 2));
  JS_SetPropertyStr(ctx, path_obj, "resolve",
                    JS_NewCFunction(ctx, JsFsPathResolve, "resolve", 2));
  JS_SetPropertyStr(ctx, path_obj, "dirname",
                    JS_NewCFunction(ctx, JsFsPathDirname, "dirname", 1));
  JS_SetPropertyStr(ctx, path_obj, "basename",
                    JS_NewCFunction(ctx, JsFsPathBasename, "basename", 2));
  JS_SetPropertyStr(ctx, path_obj, "extname",
                    JS_NewCFunction(ctx, JsFsPathExtname, "extname", 1));
  JS_SetPropertyStr(ctx, path_obj, "normalize",
                    JS_NewCFunction(ctx, JsFsPathNormalize, "normalize", 1));

  JS_SetPropertyStr(ctx, fs_obj, "path", path_obj);
  JS_SetPropertyStr(ctx, global, "fs", fs_obj);
  JS_FreeValue(ctx, global);
}

}  // namespace module
