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

#include "module/proc.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "completion.h"
#include "nlohmann/json.hpp"
#include "perception/loader.h"
#include "perception/memory.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/shared_memory_pipe.h"
#include "perception/storage_manager.h"
#include "perception/time.h"

namespace perception {
std::shared_ptr<SharedMemoryPipe> GetFileDescriptorPipe(int fd);
}  // namespace perception

namespace module {
namespace {

// Standard input file descriptor index.
constexpr int kStdinFd = 0;

// Standard output file descriptor index.
constexpr int kStdoutFd = 1;

// Standard error file descriptor index.
constexpr int kStderrFd = 2;

// Polling interval in milliseconds while waiting for process completion.
constexpr int kPollIntervalMs = 5;

// Buffer size in bytes for reading from shared memory pipes.
constexpr size_t kPipeChunkBytes = 4096;

// Maximum directory entries fetched per StorageManager::ReadDirectory call.
constexpr uint64 kReadDirBatchSize = 256;

// Maximum CPU core percentages queried from GetProcessHealthMetrics.
constexpr size_t kMaxCpuCores = 64;

// Root directory containing installed applications.
constexpr std::string_view kApplicationsDirectory = "/Applications";

// Launcher metadata filename inside each application directory.
constexpr std::string_view kLauncherJsonFilename = "launcher.json";

struct InstalledAppDetails {
  std::string name;
  std::string path;
  std::string description;
  bool terminal = false;
};

enum class StageKind {
  kCommand,
  kFromData,
  kFromFile,
  kTransform,
  kTee,
  kSeq,
  kMerge,
};

struct PipelineData;

struct PipelineStage {
  StageKind kind = StageKind::kCommand;
  std::string target;
  std::vector<std::string> args;
  std::string data;
  std::string file_path;
  bool append = false;
  JSValue transform_fn = JS_UNDEFINED;
  std::vector<std::shared_ptr<PipelineData>> sub_pipelines;
};

struct PipelineData {
  JSRuntime* rt = nullptr;
  std::vector<PipelineStage> stages;
  std::string out_file;
  bool out_append = false;
  std::string err_file;
  bool err_append = false;
  bool err_to_out = false;
  bool null_out = false;
  bool null_err = false;
  std::vector<perception::ProcessId> active_pids;

  ~PipelineData() {
    if (!rt)
      return;
    for (auto& stage : stages) {
      if (!JS_IsUndefined(stage.transform_fn))
        JS_FreeValueRT(rt, stage.transform_fn);
    }
  }
};

struct ChildProcessData {
  size_t job_id = 0;
  perception::ProcessId pid = 0;
  std::string name;
  std::string command_line;
  std::shared_ptr<perception::SharedMemoryPipe> stdin_pipe;
  std::shared_ptr<perception::SharedMemoryPipe> stdout_pipe;
  std::shared_ptr<perception::SharedMemoryPipe> stderr_pipe;
};

JSClassID g_pipeline_class_id = 0;
JSClassID g_child_process_class_id = 0;

std::string JsValueToStdString(JSContext* ctx, JSValueConst val) {
  size_t len = 0;
  const char* cstr = JS_ToCStringLen(ctx, &len, val);
  if (!cstr)
    return {};
  std::string result(cstr, len);
  JS_FreeCString(ctx, cstr);
  return result;
}

JSValue MakeResolvedPromise(JSContext* ctx, JSValue val) {
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

JSValue MakeRejectedPromise(JSContext* ctx, std::string_view message) {
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

bool ReadEntireFileBytes(std::string_view path, std::string& out,
                         std::string& error_out) {
  std::ifstream file{std::string(path), std::ios::binary};
  if (!file.is_open()) {
    error_out = "Cannot open file: " + std::string(path);
    return false;
  }
  std::ostringstream ss;
  ss << file.rdbuf();
  out = ss.str();
  return true;
}

bool WriteFileBytes(std::string_view path, std::string_view data, bool append,
                    std::string& error_out) {
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
  if (!data.empty())
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
  return file.good();
}

const std::vector<InstalledAppDetails>& ScanInstalledApplications() {
  static std::vector<InstalledAppDetails> cached_apps;
  if (!cached_apps.empty())
    return cached_apps;

  auto storage = perception::GetService<perception::StorageManager>();
  uint64 index = 0;
  while (true) {
    perception::ReadDirectoryRequest req;
    req.path = std::string(kApplicationsDirectory);
    req.first_index = index;
    req.maximum_number_of_entries = kReadDirBatchSize;
    auto status_or_resp = storage.ReadDirectory(req);
    if (!status_or_resp)
      break;
    for (const auto& entry : status_or_resp->entries) {
      if (entry.type != perception::DirectoryEntry::Type::DIRECTORY)
        continue;
      InstalledAppDetails info;
      info.name = entry.name;
      info.path = std::string(kApplicationsDirectory) + "/" + entry.name;
      std::string launcher_path =
          info.path + "/" + std::string(kLauncherJsonFilename);
      auto launcher_stat = storage.GetFileStatistics(
          perception::RequestWithFilePath(launcher_path));
      if (launcher_stat && launcher_stat->exists) {
        std::string json_text;
        std::string unused_err;
        if (ReadEntireFileBytes(launcher_path, json_text, unused_err)) {
          auto parsed = nlohmann::json::parse(json_text, nullptr, false);
          if (parsed.is_object()) {
            if (parsed.contains("name") && parsed["name"].is_string())
              info.name = parsed["name"].get<std::string>();
            if (parsed.contains("description") &&
                parsed["description"].is_string()) {
              info.description = parsed["description"].get<std::string>();
            }
            if (parsed.contains("terminal") && parsed["terminal"].is_boolean())
              info.terminal = parsed["terminal"].get<bool>();
          }
        }
      }
      cached_apps.push_back(std::move(info));
    }
    if (!status_or_resp->has_more_entries || status_or_resp->entries.empty())
      break;
    index += status_or_resp->entries.size();
  }
  std::sort(cached_apps.begin(), cached_apps.end(),
            [](const InstalledAppDetails& a, const InstalledAppDetails& b) {
              return a.name < b.name;
            });
  return cached_apps;
}

bool IsInstalledApplicationName(std::string_view name) {
  if (name.empty() || name.find('/') != std::string_view::npos)
    return false;
  std::string app_dir =
      std::string(kApplicationsDirectory) + "/" + std::string(name);
  auto status_or_stat =
      perception::GetService<perception::StorageManager>().GetFileStatistics(
          {app_dir, false});
  return status_or_stat && status_or_stat->exists &&
         status_or_stat->type == perception::DirectoryEntry::Type::DIRECTORY;
}

std::string ResolveLaunchTarget(std::string_view cwd,
                                std::string_view target) {
  if (target.empty())
    return {};
  if (target.starts_with("/") || target.starts_with("./") ||
      target.starts_with("../")) {
    return NormalizeLexicalPath(cwd, target);
  }
  std::string candidate = NormalizeLexicalPath(cwd, target);
  auto status_or_stat =
      perception::GetService<perception::StorageManager>().GetFileStatistics(
          {candidate, false});
  if (status_or_stat && status_or_stat->exists) {
    if (target.find('/') != std::string_view::npos ||
        status_or_stat->type == perception::DirectoryEntry::Type::FILE ||
        !IsInstalledApplicationName(target)) {
      return candidate;
    }
  }
  return std::string(target);
}

void WaitForPid(JsEngine& engine, perception::ProcessId pid) {
  if (pid == 0)
    return;
  bool terminated = false;
  perception::MessageId msg_id = perception::NotifyUponProcessTermination(
      pid, [&terminated]() { terminated = true; });
  while (!terminated && perception::DoesProcessExist(pid)) {
    perception::FinishAnyPendingWork();
    if (engine.IsInterruptRequested()) {
      perception::TerminateProcesss(pid);
      break;
    }
    if (!terminated && perception::DoesProcessExist(pid)) {
      perception::SleepForDuration(
          std::chrono::milliseconds(kPollIntervalMs));
    }
  }
  perception::StopNotifyingUponProcessTermination(msg_id);
}

std::shared_ptr<PipelineData> ClonePipelineData(JSContext* ctx,
                                                const PipelineData& src) {
  auto dst = std::make_shared<PipelineData>();
  dst->rt = JS_GetRuntime(ctx);
  dst->out_file = src.out_file;
  dst->out_append = src.out_append;
  dst->err_file = src.err_file;
  dst->err_append = src.err_append;
  dst->err_to_out = src.err_to_out;
  dst->null_out = src.null_out;
  dst->null_err = src.null_err;
  dst->stages.reserve(src.stages.size());
  for (const auto& s : src.stages) {
    PipelineStage copy;
    copy.kind = s.kind;
    copy.target = s.target;
    copy.args = s.args;
    copy.data = s.data;
    copy.file_path = s.file_path;
    copy.append = s.append;
    if (!JS_IsUndefined(s.transform_fn))
      copy.transform_fn = JS_DupValue(ctx, s.transform_fn);
    for (const auto& sub : s.sub_pipelines) {
      if (sub)
        copy.sub_pipelines.push_back(ClonePipelineData(ctx, *sub));
    }
    dst->stages.push_back(std::move(copy));
  }
  return dst;
}

bool ExtractUint8ArrayBytes(JSContext* ctx, JSValueConst val,
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

bool AppendStageFromJsValue(JSContext* ctx, PipelineData& pipeline,
                            JSValueConst stage_val, std::string& error_out) {
  if (auto* other = static_cast<PipelineData*>(
          JS_GetOpaque(stage_val, g_pipeline_class_id))) {
    auto cloned = ClonePipelineData(ctx, *other);
    for (auto& st : cloned->stages) {
      pipeline.stages.push_back(std::move(st));
      st.transform_fn = JS_UNDEFINED;
    }
    if (!cloned->out_file.empty()) {
      pipeline.out_file = cloned->out_file;
      pipeline.out_append = cloned->out_append;
    }
    if (!cloned->err_file.empty()) {
      pipeline.err_file = cloned->err_file;
      pipeline.err_append = cloned->err_append;
    }
    if (cloned->err_to_out)
      pipeline.err_to_out = true;
    if (cloned->null_out)
      pipeline.null_out = true;
    if (cloned->null_err)
      pipeline.null_err = true;
    return true;
  }
  if (JS_IsFunction(ctx, stage_val)) {
    PipelineStage stage;
    stage.kind = StageKind::kTransform;
    stage.transform_fn = JS_DupValue(ctx, stage_val);
    pipeline.stages.push_back(std::move(stage));
    return true;
  }
  error_out = "Pipeline stage must be a Command, Pipeline, or function";
  return false;
}

bool ExecutePipelineToBuffer(JSContext* ctx, JsEngine& engine,
                             PipelineData& pipeline, bool has_input,
                             const std::string& initial_input,
                             bool capture_final_stdout,
                             std::string& stdout_out,
                             std::string& error_out);

bool RunSingleCommandStage(JSContext* ctx, JsEngine& engine,
                           PipelineData& parent_pipeline,
                           const PipelineStage& stage, bool has_input,
                           const std::string& input_bytes,
                           bool is_last_stage, bool capture_stdout,
                           std::string& output_bytes, std::string& error_out) {
  (void)ctx;
  std::string resolved_target =
      ResolveLaunchTarget(engine.Cwd(), stage.target);
  if (resolved_target.empty()) {
    error_out = "Command target cannot be empty";
    return false;
  }

  bool direct_stdout_to_terminal =
      is_last_stage && !capture_stdout && parent_pipeline.out_file.empty() &&
      !parent_pipeline.null_out;
  bool direct_stderr_to_terminal =
      parent_pipeline.err_file.empty() && !parent_pipeline.null_err &&
      !parent_pipeline.err_to_out;

  if (!has_input && direct_stdout_to_terminal && direct_stderr_to_terminal) {
    perception::LoadApplicationRequest req;
    req.name = resolved_target;
    req.arguments = stage.args;
    req.create_as_child = false;
    req.stdin_pipe = perception::GetFileDescriptorPipe(kStdinFd);
    req.stdout_pipe = perception::GetFileDescriptorPipe(kStdoutFd);
    req.stderr_pipe = perception::GetFileDescriptorPipe(kStderrFd);

    auto status_or_resp =
        perception::GetService<perception::Loader>().LaunchApplication(req);
    if (!status_or_resp) {
      error_out = "Failed to launch \"" + stage.target + "\"";
      return false;
    }
    perception::ProcessId pid = status_or_resp->process;
    parent_pipeline.active_pids.push_back(pid);
    WaitForPid(engine, pid);
    return true;
  }

  std::shared_ptr<perception::SharedMemoryPipe> in_pipe;
  if (has_input) {
    in_pipe = perception::SharedMemoryPipe::Create();
    if (!in_pipe || !in_pipe->IsValid()) {
      error_out = "Failed to allocate stdin pipe";
      return false;
    }
    in_pipe->AddWriter();
  } else {
    in_pipe = perception::GetFileDescriptorPipe(kStdinFd);
  }

  std::shared_ptr<perception::SharedMemoryPipe> out_pipe;
  bool read_out_pipe = false;
  if (is_last_stage && parent_pipeline.null_out) {
    out_pipe = nullptr;
  } else if (direct_stdout_to_terminal) {
    out_pipe = perception::GetFileDescriptorPipe(kStdoutFd);
  } else {
    out_pipe = perception::SharedMemoryPipe::Create();
    if (!out_pipe || !out_pipe->IsValid()) {
      if (has_input && in_pipe)
        in_pipe->CloseWriter();
      error_out = "Failed to allocate stdout pipe";
      return false;
    }
    out_pipe->AddReader();
    read_out_pipe = true;
  }

  std::shared_ptr<perception::SharedMemoryPipe> err_pipe;
  bool read_err_pipe = false;
  if (parent_pipeline.null_err) {
    err_pipe = nullptr;
  } else if (parent_pipeline.err_to_out) {
    err_pipe = out_pipe;
  } else if (!parent_pipeline.err_file.empty()) {
    err_pipe = perception::SharedMemoryPipe::Create();
    if (err_pipe && err_pipe->IsValid()) {
      err_pipe->AddReader();
      read_err_pipe = true;
    }
  } else {
    err_pipe = perception::GetFileDescriptorPipe(kStderrFd);
  }

  perception::LoadApplicationRequest req;
  req.name = resolved_target;
  req.arguments = stage.args;
  req.create_as_child = false;
  req.stdin_pipe = in_pipe;
  req.stdout_pipe = out_pipe;
  req.stderr_pipe = err_pipe;

  auto status_or_resp =
      perception::GetService<perception::Loader>().LaunchApplication(req);
  if (!status_or_resp) {
    if (has_input && in_pipe)
      in_pipe->CloseWriter();
    if (read_out_pipe && out_pipe)
      out_pipe->CloseReader();
    if (read_err_pipe && err_pipe)
      err_pipe->CloseReader();
    error_out = "Failed to launch \"" + stage.target + "\"";
    return false;
  }

  perception::ProcessId pid = status_or_resp->process;
  parent_pipeline.active_pids.push_back(pid);

  size_t input_offset = 0;
  bool in_writer_closed = !has_input;
  if (has_input && input_bytes.empty()) {
    in_pipe->CloseWriter();
    in_writer_closed = true;
  }

  std::string captured_err;
  char buffer[kPipeChunkBytes];
  bool terminated = false;
  perception::MessageId msg_id = perception::NotifyUponProcessTermination(
      pid, [&terminated]() { terminated = true; });

  while (true) {
    perception::FinishAnyPendingWork();
    if (engine.IsInterruptRequested()) {
      perception::TerminateProcesss(pid);
      break;
    }

    bool made_progress = false;
    if (!in_writer_closed && in_pipe) {
      if (in_pipe->IsBrokenPipe() ||
          (!perception::DoesProcessExist(pid) && terminated)) {
        in_pipe->CloseWriter();
        in_writer_closed = true;
      } else {
        size_t remaining = input_bytes.size() - input_offset;
        long written =
            in_pipe->Write(input_bytes.data() + input_offset, remaining, true);
        if (written > 0) {
          input_offset += static_cast<size_t>(written);
          made_progress = true;
          if (input_offset >= input_bytes.size()) {
            in_pipe->CloseWriter();
            in_writer_closed = true;
          }
        } else if (written < 0 && written != -11) {
          in_pipe->CloseWriter();
          in_writer_closed = true;
        }
      }
    }

    if (read_out_pipe && out_pipe) {
      while (true) {
        long n = out_pipe->Read(buffer, sizeof(buffer), true);
        if (n > 0) {
          output_bytes.append(buffer, static_cast<size_t>(n));
          made_progress = true;
        } else {
          break;
        }
      }
    }

    if (read_err_pipe && err_pipe) {
      while (true) {
        long n = err_pipe->Read(buffer, sizeof(buffer), true);
        if (n > 0) {
          captured_err.append(buffer, static_cast<size_t>(n));
          made_progress = true;
        } else {
          break;
        }
      }
    }

    bool child_alive = !terminated && perception::DoesProcessExist(pid);
    bool out_has_data = read_out_pipe && out_pipe && out_pipe->BufferedBytes() > 0;
    bool err_has_data = read_err_pipe && err_pipe && err_pipe->BufferedBytes() > 0;

    if (!child_alive && !out_has_data && !err_has_data)
      break;

    if (!made_progress) {
      perception::SleepForDuration(
          std::chrono::milliseconds(kPollIntervalMs));
    }
  }

  perception::StopNotifyingUponProcessTermination(msg_id);
  if (!in_writer_closed && in_pipe)
    in_pipe->CloseWriter();
  if (read_out_pipe && out_pipe)
    out_pipe->CloseReader();
  if (read_err_pipe && err_pipe)
    err_pipe->CloseReader();

  if (read_err_pipe && !parent_pipeline.err_file.empty()) {
    if (!WriteFileBytes(parent_pipeline.err_file, captured_err,
                        parent_pipeline.err_append, error_out)) {
      return false;
    }
  }

  return true;
}

bool ExecutePipelineToBuffer(JSContext* ctx, JsEngine& engine,
                             PipelineData& pipeline, bool has_input,
                             const std::string& initial_input,
                             bool capture_final_stdout,
                             std::string& stdout_out,
                             std::string& error_out) {
  std::string current_data = initial_input;
  bool current_has_data = has_input;

  for (size_t i = 0; i < pipeline.stages.size(); ++i) {
    const PipelineStage& stage = pipeline.stages[i];
    bool is_last = (i + 1 == pipeline.stages.size());

    switch (stage.kind) {
      case StageKind::kFromData:
        current_data = stage.data;
        current_has_data = true;
        break;

      case StageKind::kFromFile:
        if (!ReadEntireFileBytes(stage.file_path, current_data, error_out))
          return false;
        current_has_data = true;
        break;

      case StageKind::kTee:
        if (!WriteFileBytes(stage.file_path, current_data, stage.append,
                            error_out)) {
          return false;
        }
        current_has_data = true;
        break;

      case StageKind::kTransform: {
        JSValue arg = JS_NewStringLen(ctx, current_data.data(),
                                      current_data.size());
        JSValue ret =
            JS_Call(ctx, stage.transform_fn, JS_UNDEFINED, 1, &arg);
        JS_FreeValue(ctx, arg);
        if (JS_IsException(ret)) {
          JSValue exc = JS_GetException(ctx);
          error_out = JsValueToStdString(ctx, exc);
          JS_FreeValue(ctx, exc);
          return false;
        }
        bool had_exc = false;
        JSValue settled = engine.AwaitValue(ret, had_exc);
        if (had_exc) {
          JS_FreeValue(ctx, settled);
          error_out = "Transform stage threw an exception";
          return false;
        }
        if (JS_IsUndefined(settled) || JS_IsNull(settled)) {
          current_data.clear();
        } else if (!ExtractUint8ArrayBytes(ctx, settled, current_data)) {
          current_data = JsValueToStdString(ctx, settled);
        }
        JS_FreeValue(ctx, settled);
        current_has_data = true;
        break;
      }

      case StageKind::kSeq: {
        std::string combined;
        for (const auto& sub : stage.sub_pipelines) {
          if (!sub)
            continue;
          std::string piece;
          if (!ExecutePipelineToBuffer(ctx, engine, *sub, current_has_data,
                                       current_data, true, piece, error_out)) {
            return false;
          }
          combined.append(piece);
        }
        current_data = std::move(combined);
        current_has_data = true;
        break;
      }

      case StageKind::kMerge: {
        std::string merged;
        std::shared_ptr<perception::SharedMemoryPipe> shared_out =
            perception::SharedMemoryPipe::Create();
        if (shared_out && shared_out->IsValid())
          shared_out->AddReader();

        struct RunningMergeChild {
          perception::ProcessId pid = 0;
          bool terminated = false;
          perception::MessageId msg_id = 0;
        };
        std::vector<std::unique_ptr<RunningMergeChild>> running_children;

        for (const auto& sub : stage.sub_pipelines) {
          if (!sub)
            continue;
          if (sub->stages.size() == 1 &&
              sub->stages[0].kind == StageKind::kCommand && !current_has_data &&
              shared_out && shared_out->IsValid()) {
            perception::LoadApplicationRequest req;
            req.name = ResolveLaunchTarget(engine.Cwd(), sub->stages[0].target);
            req.arguments = sub->stages[0].args;
            req.create_as_child = false;
            req.stdout_pipe = shared_out;
            req.stderr_pipe = sub->err_to_out
                                  ? shared_out
                                  : perception::GetFileDescriptorPipe(kStderrFd);
            auto status_or_resp =
                perception::GetService<perception::Loader>().LaunchApplication(
                    req);
            if (!status_or_resp) {
              error_out = "Failed to launch \"" + sub->stages[0].target + "\"";
              break;
            }
            auto child = std::make_unique<RunningMergeChild>();
            child->pid = status_or_resp->process;
            pipeline.active_pids.push_back(child->pid);
            RunningMergeChild* raw_ptr = child.get();
            child->msg_id = perception::NotifyUponProcessTermination(
                child->pid, [raw_ptr]() { raw_ptr->terminated = true; });
            running_children.push_back(std::move(child));
          } else {
            std::string piece;
            if (!ExecutePipelineToBuffer(ctx, engine, *sub, current_has_data,
                                         current_data, true, piece,
                                         error_out)) {
              for (auto& rc : running_children)
                perception::StopNotifyingUponProcessTermination(rc->msg_id);
              if (shared_out && shared_out->IsValid())
                shared_out->CloseReader();
              return false;
            }
            merged.append(piece);
          }
        }

        char buf[kPipeChunkBytes];
        while (!running_children.empty() && error_out.empty()) {
          perception::FinishAnyPendingWork();
          if (engine.IsInterruptRequested()) {
            for (auto& rc : running_children)
              perception::TerminateProcesss(rc->pid);
            break;
          }
          bool progress = false;
          if (shared_out && shared_out->IsValid()) {
            while (true) {
              long n = shared_out->Read(buf, sizeof(buf), true);
              if (n > 0) {
                merged.append(buf, static_cast<size_t>(n));
                progress = true;
              } else {
                break;
              }
            }
          }
          bool any_alive = false;
          for (auto& rc : running_children) {
            if (!rc->terminated && perception::DoesProcessExist(rc->pid))
              any_alive = true;
          }
          bool has_buffered =
              shared_out && shared_out->IsValid() &&
              shared_out->BufferedBytes() > 0;
          if (!any_alive && !has_buffered)
            break;
          if (!progress) {
            perception::SleepForDuration(
                std::chrono::milliseconds(kPollIntervalMs));
          }
        }

        for (auto& rc : running_children)
          perception::StopNotifyingUponProcessTermination(rc->msg_id);
        if (shared_out && shared_out->IsValid())
          shared_out->CloseReader();
        if (!error_out.empty())
          return false;

        current_data = std::move(merged);
        current_has_data = true;
        break;
      }

      case StageKind::kCommand: {
        std::string next_output;
        bool need_capture = !is_last || capture_final_stdout ||
                            !pipeline.out_file.empty() || pipeline.null_out;
        if (!RunSingleCommandStage(ctx, engine, pipeline, stage,
                                   current_has_data, current_data, is_last,
                                   need_capture, next_output, error_out)) {
          return false;
        }
        current_data = std::move(next_output);
        current_has_data = true;
        break;
      }
    }
  }

  if (!pipeline.out_file.empty()) {
    if (!WriteFileBytes(pipeline.out_file, current_data, pipeline.out_append,
                        error_out)) {
      return false;
    }
  } else if ( !capture_final_stdout && !pipeline.null_out &&
             !pipeline.stages.empty() &&
             pipeline.stages.back().kind != StageKind::kCommand &&
             !current_data.empty()) {
    std::fwrite(current_data.data(), 1, current_data.size(), stdout);
    std::fflush(stdout);
  }

  if (capture_final_stdout && !pipeline.null_out)
    stdout_out = std::move(current_data);
  else
    stdout_out.clear();

  return true;
}

void JsPipelineFinalizer(JSRuntime* rt, JSValue val) {
  auto* data = static_cast<PipelineData*>(
      JS_GetOpaque(val, g_pipeline_class_id));
  if (data) {
    data->rt = rt;
    delete data;
  }
}

void JsChildProcessFinalizer(JSRuntime* rt, JSValue val) {
  (void)rt;
  auto* data = static_cast<ChildProcessData*>(
      JS_GetOpaque(val, g_child_process_class_id));
  delete data;
}

JSValue CreatePipelineJsObject(JSContext* ctx,
                               std::shared_ptr<PipelineData> data) {
  JSValue obj = JS_NewObjectClass(ctx, static_cast<int>(g_pipeline_class_id));
  if (JS_IsException(obj))
    return JS_EXCEPTION;
  auto* raw = new PipelineData();
  raw->rt = JS_GetRuntime(ctx);
  if (data) {
    auto cloned = ClonePipelineData(ctx, *data);
    raw->stages = std::move(cloned->stages);
    raw->out_file = std::move(cloned->out_file);
    raw->out_append = cloned->out_append;
    raw->err_file = std::move(cloned->err_file);
    raw->err_append = cloned->err_append;
    raw->err_to_out = cloned->err_to_out;
    raw->null_out = cloned->null_out;
    raw->null_err = cloned->null_err;
  }
  JS_SetOpaque(obj, raw);
  return obj;
}

PipelineData* GetPipelineOpaque(JSContext* ctx, JSValueConst this_val) {
  return static_cast<PipelineData*>(
      JS_GetOpaque2(ctx, this_val, g_pipeline_class_id));
}

bool ParseAppendOption(JSContext* ctx, int argc, JSValueConst* argv,
                       int opt_index) {
  if (argc <= opt_index || !JS_IsObject(argv[opt_index]))
    return false;
  JSValue val = JS_GetPropertyStr(ctx, argv[opt_index], "append");
  bool append = JS_ToBool(ctx, val) > 0;
  JS_FreeValue(ctx, val);
  return append;
}

JSValue JsPipelinePipe(JSContext* ctx, JSValueConst this_val, int argc,
                       JSValueConst* argv) {
  PipelineData* self = GetPipelineOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  if (argc < 1)
    return JS_ThrowTypeError(ctx, ".pipe() requires a stage argument");
  auto next_data = ClonePipelineData(ctx, *self);
  std::string err;
  for (int i = 0; i < argc; ++i) {
    if (!AppendStageFromJsValue(ctx, *next_data, argv[i], err))
      return JS_ThrowTypeError(ctx, "%s", err.c_str());
  }
  return CreatePipelineJsObject(ctx, next_data);
}

JSValue JsPipelineOut(JSContext* ctx, JSValueConst this_val, int argc,
                      JSValueConst* argv) {
  PipelineData* self = GetPipelineOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  if (argc < 1)
    return JS_ThrowTypeError(ctx, ".out() requires a file path");
  JsEngine* engine = GetJsEngine(ctx);
  std::string path = JsValueToStdString(ctx, argv[0]);
  self->out_file =
      NormalizeLexicalPath(engine ? engine->Cwd() : "/", path);
  self->out_append = ParseAppendOption(ctx, argc, argv, 1);
  self->null_out = false;
  return JS_DupValue(ctx, this_val);
}

JSValue JsPipelineErr(JSContext* ctx, JSValueConst this_val, int argc,
                      JSValueConst* argv) {
  PipelineData* self = GetPipelineOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  if (argc < 1)
    return JS_ThrowTypeError(ctx, ".err() requires a file path");
  JsEngine* engine = GetJsEngine(ctx);
  std::string path = JsValueToStdString(ctx, argv[0]);
  self->err_file =
      NormalizeLexicalPath(engine ? engine->Cwd() : "/", path);
  self->err_append = ParseAppendOption(ctx, argc, argv, 1);
  self->null_err = false;
  return JS_DupValue(ctx, this_val);
}

JSValue JsPipelineErrToOut(JSContext* ctx, JSValueConst this_val, int argc,
                           JSValueConst* argv) {
  (void)argc;
  (void)argv;
  PipelineData* self = GetPipelineOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  self->err_to_out = true;
  self->null_err = false;
  return JS_DupValue(ctx, this_val);
}

JSValue JsPipelineNullOut(JSContext* ctx, JSValueConst this_val, int argc,
                          JSValueConst* argv) {
  (void)argc;
  (void)argv;
  PipelineData* self = GetPipelineOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  self->null_out = true;
  self->out_file.clear();
  return JS_DupValue(ctx, this_val);
}

JSValue JsPipelineNullErr(JSContext* ctx, JSValueConst this_val, int argc,
                          JSValueConst* argv) {
  (void)argc;
  (void)argv;
  PipelineData* self = GetPipelineOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  self->null_err = true;
  self->err_file.clear();
  return JS_DupValue(ctx, this_val);
}

JSValue JsPipelineTee(JSContext* ctx, JSValueConst this_val, int argc,
                      JSValueConst* argv) {
  PipelineData* self = GetPipelineOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  if (argc < 1)
    return JS_ThrowTypeError(ctx, ".tee() requires a file path");
  JsEngine* engine = GetJsEngine(ctx);
  std::string path = JsValueToStdString(ctx, argv[0]);
  auto next_data = ClonePipelineData(ctx, *self);
  PipelineStage stage;
  stage.kind = StageKind::kTee;
  stage.file_path =
      NormalizeLexicalPath(engine ? engine->Cwd() : "/", path);
  stage.append = ParseAppendOption(ctx, argc, argv, 1);
  next_data->stages.push_back(std::move(stage));
  return CreatePipelineJsObject(ctx, next_data);
}

JSValue JsPipelineText(JSContext* ctx, JSValueConst this_val, int argc,
                       JSValueConst* argv) {
  PipelineData* self = GetPipelineOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  JsEngine* engine = GetJsEngine(ctx);
  if (!engine)
    return MakeRejectedPromise(ctx, "Missing JsEngine instance");

  bool trim = true;
  if (argc >= 1 && JS_IsObject(argv[0])) {
    JSValue trim_val = JS_GetPropertyStr(ctx, argv[0], "trim");
    if (!JS_IsUndefined(trim_val))
      trim = JS_ToBool(ctx, trim_val) > 0;
    JS_FreeValue(ctx, trim_val);
  }

  std::string out;
  std::string err;
  if (!ExecutePipelineToBuffer(ctx, *engine, *self, false, {}, true, out,
                               err)) {
    return MakeRejectedPromise(ctx, err);
  }

  if (trim) {
    if (!out.empty() && out.back() == '\n')
      out.pop_back();
    if (!out.empty() && out.back() == '\r')
      out.pop_back();
  }
  return MakeResolvedPromise(ctx, JS_NewStringLen(ctx, out.data(), out.size()));
}

JSValue JsPipelineLines(JSContext* ctx, JSValueConst this_val, int argc,
                        JSValueConst* argv) {
  (void)argc;
  (void)argv;
  PipelineData* self = GetPipelineOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  JsEngine* engine = GetJsEngine(ctx);
  if (!engine)
    return MakeRejectedPromise(ctx, "Missing JsEngine instance");

  std::string out;
  std::string err;
  if (!ExecutePipelineToBuffer(ctx, *engine, *self, false, {}, true, out,
                               err)) {
    return MakeRejectedPromise(ctx, err);
  }

  JSValue arr = JS_NewArray(ctx);
  uint32_t idx = 0;
  size_t pos = 0;
  while (pos < out.size()) {
    size_t nl = out.find('\n', pos);
    size_t end = (nl == std::string::npos) ? out.size() : nl;
    size_t len = end - pos;
    if (len > 0 && out[pos + len - 1] == '\r')
      --len;
    JS_SetPropertyUint32(
        ctx, arr, idx++, JS_NewStringLen(ctx, out.data() + pos, len));
    if (nl == std::string::npos)
      break;
    pos = nl + 1;
  }
  return MakeResolvedPromise(ctx, arr);
}

JSValue JsPipelineJson(JSContext* ctx, JSValueConst this_val, int argc,
                       JSValueConst* argv) {
  (void)argc;
  (void)argv;
  PipelineData* self = GetPipelineOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  JsEngine* engine = GetJsEngine(ctx);
  if (!engine)
    return MakeRejectedPromise(ctx, "Missing JsEngine instance");

  std::string out;
  std::string err;
  if (!ExecutePipelineToBuffer(ctx, *engine, *self, false, {}, true, out,
                               err)) {
    return MakeRejectedPromise(ctx, err);
  }

  JSValue parsed = JS_ParseJSON(ctx, out.c_str(), out.size(), "<pipe.json>");
  if (JS_IsException(parsed)) {
    JSValue exc = JS_GetException(ctx);
    std::string msg = JsValueToStdString(ctx, exc);
    JS_FreeValue(ctx, exc);
    return MakeRejectedPromise(ctx, msg);
  }
  return MakeResolvedPromise(ctx, parsed);
}

JSValue JsPipelineBytes(JSContext* ctx, JSValueConst this_val, int argc,
                        JSValueConst* argv) {
  (void)argc;
  (void)argv;
  PipelineData* self = GetPipelineOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  JsEngine* engine = GetJsEngine(ctx);
  if (!engine)
    return MakeRejectedPromise(ctx, "Missing JsEngine instance");

  std::string out;
  std::string err;
  if (!ExecutePipelineToBuffer(ctx, *engine, *self, false, {}, true, out,
                               err)) {
    return MakeRejectedPromise(ctx, err);
  }

  JSValue ab = JS_NewArrayBufferCopy(
      ctx, reinterpret_cast<const uint8_t*>(out.data()), out.size());
  if (JS_IsException(ab))
    return JS_EXCEPTION;
  JSValue u8 = JS_NewTypedArray(ctx, 1, &ab, JS_TYPED_ARRAY_UINT8);
  JS_FreeValue(ctx, ab);
  if (JS_IsException(u8))
    return JS_EXCEPTION;
  return MakeResolvedPromise(ctx, u8);
}

JSValue CreateChildProcessJsObject(JSContext* ctx, ChildProcessData info);

JSValue JsPipelineBg(JSContext* ctx, JSValueConst this_val, int argc,
                     JSValueConst* argv) {
  (void)argc;
  (void)argv;
  PipelineData* self = GetPipelineOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  JsEngine* engine = GetJsEngine(ctx);
  if (!engine)
    return JS_ThrowInternalError(ctx, "Missing JsEngine instance");

  const PipelineStage* cmd_stage = nullptr;
  for (const auto& s : self->stages) {
    if (s.kind == StageKind::kCommand) {
      cmd_stage = &s;
      break;
    }
  }
  if (!cmd_stage)
    return JS_ThrowTypeError(ctx, ".bg() requires a command stage");

  std::string resolved = ResolveLaunchTarget(engine->Cwd(), cmd_stage->target);
  perception::LoadApplicationRequest req;
  req.name = resolved;
  req.arguments = cmd_stage->args;
  req.create_as_child = false;

  auto status_or_resp =
      perception::GetService<perception::Loader>().LaunchApplication(req);
  if (!status_or_resp) {
    return JS_ThrowInternalError(ctx, "Failed to launch \"%s\"",
                                 cmd_stage->target.c_str());
  }

  perception::ProcessId pid = status_or_resp->process;
  self->active_pids.push_back(pid);

  std::string cmd_line = cmd_stage->target;
  for (const auto& a : cmd_stage->args) {
    cmd_line += " ";
    cmd_line += a;
  }
  size_t job_id = engine->AddBackgroundJob(pid, cmd_stage->target, cmd_line);

  ChildProcessData child_info;
  child_info.job_id = job_id;
  child_info.pid = pid;
  child_info.name = cmd_stage->target;
  child_info.command_line = cmd_line;
  return CreateChildProcessJsObject(ctx, std::move(child_info));
}

JSValue JsPipelineKill(JSContext* ctx, JSValueConst this_val, int argc,
                       JSValueConst* argv) {
  (void)argc;
  (void)argv;
  PipelineData* self = GetPipelineOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  int killed = 0;
  for (perception::ProcessId pid : self->active_pids) {
    if (pid != 0 && perception::DoesProcessExist(pid)) {
      perception::TerminateProcesss(pid);
      ++killed;
    }
  }
  return MakeResolvedPromise(ctx, JS_NewInt32(ctx, killed));
}

JSValue ExecutePipelineAsVoidPromise(JSContext* ctx, PipelineData& self) {
  JsEngine* engine = GetJsEngine(ctx);
  if (!engine)
    return MakeRejectedPromise(ctx, "Missing JsEngine instance");
  std::string unused_out;
  std::string err;
  if (!ExecutePipelineToBuffer(ctx, *engine, self, false, {}, false,
                               unused_out, err)) {
    return MakeRejectedPromise(ctx, err);
  }
  return MakeResolvedPromise(ctx, JS_UNDEFINED);
}

JSValue JsPipelineThen(JSContext* ctx, JSValueConst this_val, int argc,
                       JSValueConst* argv) {
  PipelineData* self = GetPipelineOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  JSValue promise = ExecutePipelineAsVoidPromise(ctx, *self);
  if (JS_IsException(promise))
    return JS_EXCEPTION;
  JSAtom then_atom = JS_NewAtom(ctx, "then");
  JSValue ret = JS_Invoke(ctx, promise, then_atom, argc, argv);
  JS_FreeAtom(ctx, then_atom);
  JS_FreeValue(ctx, promise);
  return ret;
}

JSValue JsPipelineCatch(JSContext* ctx, JSValueConst this_val, int argc,
                        JSValueConst* argv) {
  PipelineData* self = GetPipelineOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  JSValue promise = ExecutePipelineAsVoidPromise(ctx, *self);
  if (JS_IsException(promise))
    return JS_EXCEPTION;
  JSAtom catch_atom = JS_NewAtom(ctx, "catch");
  JSValue ret = JS_Invoke(ctx, promise, catch_atom, argc, argv);
  JS_FreeAtom(ctx, catch_atom);
  JS_FreeValue(ctx, promise);
  return ret;
}

JSValue JsPipelineFinally(JSContext* ctx, JSValueConst this_val, int argc,
                          JSValueConst* argv) {
  PipelineData* self = GetPipelineOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  JSValue promise = ExecutePipelineAsVoidPromise(ctx, *self);
  if (JS_IsException(promise))
    return JS_EXCEPTION;
  JSAtom finally_atom = JS_NewAtom(ctx, "finally");
  JSValue ret = JS_Invoke(ctx, promise, finally_atom, argc, argv);
  JS_FreeAtom(ctx, finally_atom);
  JS_FreeValue(ctx, promise);
  return ret;
}

ChildProcessData* GetChildProcessOpaque(JSContext* ctx,
                                        JSValueConst this_val) {
  return static_cast<ChildProcessData*>(
      JS_GetOpaque2(ctx, this_val, g_child_process_class_id));
}

JSValue JsChildProcessRunning(JSContext* ctx, JSValueConst this_val, int argc,
                              JSValueConst* argv) {
  (void)argc;
  (void)argv;
  ChildProcessData* self = GetChildProcessOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  bool alive = self->pid != 0 && perception::DoesProcessExist(self->pid);
  return JS_NewBool(ctx, alive);
}

JSValue JsChildProcessWait(JSContext* ctx, JSValueConst this_val, int argc,
                           JSValueConst* argv) {
  (void)argc;
  (void)argv;
  ChildProcessData* self = GetChildProcessOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  JsEngine* engine = GetJsEngine(ctx);
  if (engine && self->pid != 0)
    WaitForPid(*engine, self->pid);
  return MakeResolvedPromise(ctx, JS_UNDEFINED);
}

JSValue JsChildProcessKill(JSContext* ctx, JSValueConst this_val, int argc,
                           JSValueConst* argv) {
  (void)argc;
  (void)argv;
  ChildProcessData* self = GetChildProcessOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  int killed = 0;
  if (self->pid != 0 && perception::DoesProcessExist(self->pid)) {
    perception::TerminateProcesss(self->pid);
    killed = 1;
  }
  return MakeResolvedPromise(ctx, JS_NewInt32(ctx, killed));
}

JSValue JsChildProcessToJson(JSContext* ctx, JSValueConst this_val, int argc,
                             JSValueConst* argv) {
  (void)argc;
  (void)argv;
  ChildProcessData* self = GetChildProcessOpaque(ctx, this_val);
  if (!self)
    return JS_EXCEPTION;
  JSValue obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, obj, "jobId",
                    JS_NewInt64(ctx, static_cast<int64_t>(self->job_id)));
  JS_SetPropertyStr(ctx, obj, "pid",
                    JS_NewInt64(ctx, static_cast<int64_t>(self->pid)));
  JS_SetPropertyStr(
      ctx, obj, "name",
      JS_NewStringLen(ctx, self->name.data(), self->name.size()));
  JS_SetPropertyStr(ctx, obj, "commandLine",
                    JS_NewStringLen(ctx, self->command_line.data(),
                                    self->command_line.size()));
  bool alive = self->pid != 0 && perception::DoesProcessExist(self->pid);
  JS_SetPropertyStr(ctx, obj, "running", JS_NewBool(ctx, alive));
  return obj;
}

JSValue CreateChildProcessJsObject(JSContext* ctx, ChildProcessData info) {
  JSValue obj =
      JS_NewObjectClass(ctx, static_cast<int>(g_child_process_class_id));
  if (JS_IsException(obj))
    return JS_EXCEPTION;
  auto* raw = new ChildProcessData(std::move(info));
  JS_SetOpaque(obj, raw);
  JS_SetPropertyStr(ctx, obj, "jobId",
                    JS_NewInt64(ctx, static_cast<int64_t>(raw->job_id)));
  JS_SetPropertyStr(ctx, obj, "pid",
                    JS_NewInt64(ctx, static_cast<int64_t>(raw->pid)));
  JS_SetPropertyStr(
      ctx, obj, "name",
      JS_NewStringLen(ctx, raw->name.data(), raw->name.size()));
  JS_SetPropertyStr(
      ctx, obj, "commandLine",
      JS_NewStringLen(ctx, raw->command_line.data(), raw->command_line.size()));
  JS_SetPropertyStr(ctx, obj, "stdin", JS_NULL);
  JS_SetPropertyStr(ctx, obj, "stdout", JS_NULL);
  JS_SetPropertyStr(ctx, obj, "stderr", JS_NULL);
  return obj;
}

JSValue JsRun(JSContext* ctx, JSValueConst this_val, int argc,
              JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "run() requires a target argument");
  std::string target = JsValueToStdString(ctx, argv[0]);
  if (target.empty())
    return JS_ThrowTypeError(ctx, "run() target cannot be empty");

  auto data = std::make_shared<PipelineData>();
  data->rt = JS_GetRuntime(ctx);
  PipelineStage stage;
  stage.kind = StageKind::kCommand;
  stage.target = std::move(target);
  for (int i = 1; i < argc; ++i)
    stage.args.push_back(JsValueToStdString(ctx, argv[i]));
  data->stages.push_back(std::move(stage));
  return CreatePipelineJsObject(ctx, data);
}

JSValue JsPipe(JSContext* ctx, JSValueConst this_val, int argc,
               JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "pipe() requires at least one stage");
  auto data = std::make_shared<PipelineData>();
  data->rt = JS_GetRuntime(ctx);
  std::string err;
  for (int i = 0; i < argc; ++i) {
    if (!AppendStageFromJsValue(ctx, *data, argv[i], err))
      return JS_ThrowTypeError(ctx, "%s", err.c_str());
  }
  return CreatePipelineJsObject(ctx, data);
}

JSValue JsPipeFrom(JSContext* ctx, JSValueConst this_val, int argc,
                   JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "pipe.from() requires a data argument");
  std::string bytes;
  if (!ExtractUint8ArrayBytes(ctx, argv[0], bytes)) {
    if (JS_IsArray(ctx, argv[0]) > 0) {
      JSValue len_val = JS_GetPropertyStr(ctx, argv[0], "length");
      uint32_t len = 0;
      JS_ToUint32(ctx, &len, len_val);
      JS_FreeValue(ctx, len_val);
      for (uint32_t i = 0; i < len; ++i) {
        JSValue elem = JS_GetPropertyUint32(ctx, argv[0], i);
        bytes += JsValueToStdString(ctx, elem);
        bytes.push_back('\n');
        JS_FreeValue(ctx, elem);
      }
    } else {
      bytes = JsValueToStdString(ctx, argv[0]);
    }
  }
  auto data = std::make_shared<PipelineData>();
  data->rt = JS_GetRuntime(ctx);
  PipelineStage stage;
  stage.kind = StageKind::kFromData;
  stage.data = std::move(bytes);
  data->stages.push_back(std::move(stage));
  return CreatePipelineJsObject(ctx, data);
}

JSValue JsPipeFile(JSContext* ctx, JSValueConst this_val, int argc,
                   JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "pipe.file() requires a path argument");
  JsEngine* engine = GetJsEngine(ctx);
  std::string raw_path = JsValueToStdString(ctx, argv[0]);
  auto data = std::make_shared<PipelineData>();
  data->rt = JS_GetRuntime(ctx);
  PipelineStage stage;
  stage.kind = StageKind::kFromFile;
  stage.file_path =
      NormalizeLexicalPath(engine ? engine->Cwd() : "/", raw_path);
  data->stages.push_back(std::move(stage));
  return CreatePipelineJsObject(ctx, data);
}

JSValue BuildJoinedPipeline(JSContext* ctx, int argc, JSValueConst* argv,
                            StageKind join_kind, const char* fn_name) {
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "%s requires at least one stage", fn_name);
  auto data = std::make_shared<PipelineData>();
  data->rt = JS_GetRuntime(ctx);
  PipelineStage stage;
  stage.kind = join_kind;
  for (int i = 0; i < argc; ++i) {
    auto sub = std::make_shared<PipelineData>();
    sub->rt = JS_GetRuntime(ctx);
    std::string err;
    if (!AppendStageFromJsValue(ctx, *sub, argv[i], err))
      return JS_ThrowTypeError(ctx, "%s", err.c_str());
    stage.sub_pipelines.push_back(std::move(sub));
  }
  data->stages.push_back(std::move(stage));
  return CreatePipelineJsObject(ctx, data);
}

JSValue JsPipeSeq(JSContext* ctx, JSValueConst this_val, int argc,
                  JSValueConst* argv) {
  (void)this_val;
  return BuildJoinedPipeline(ctx, argc, argv, StageKind::kSeq, "pipe.seq()");
}

JSValue JsPipeMerge(JSContext* ctx, JSValueConst this_val, int argc,
                    JSValueConst* argv) {
  (void)this_val;
  return BuildJoinedPipeline(ctx, argc, argv, StageKind::kMerge,
                             "pipe.merge()");
}

bool ContainsCaseInsensitive(std::string_view haystack,
                             std::string_view needle) {
  if (needle.empty())
    return true;
  auto it = std::search(
      haystack.begin(), haystack.end(), needle.begin(), needle.end(),
      [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
      });
  return it != haystack.end();
}

JSValue JsProcPs(JSContext* ctx, JSValueConst this_val, int argc,
                 JSValueConst* argv) {
  (void)this_val;
  bool filter_by_pid = false;
  int64_t target_pid = -1;
  std::string name_filter;
  if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
    if (JS_IsNumber(argv[0])) {
      JS_ToInt64(ctx, &target_pid, argv[0]);
      filter_by_pid = true;
    } else {
      name_filter = JsValueToStdString(ctx, argv[0]);
    }
  }

  JSValue arr = JS_NewArray(ctx);
  uint32_t idx = 0;
  perception::ForEachProcess([&](perception::ProcessId pid) {
    if (filter_by_pid && static_cast<int64_t>(pid) != target_pid)
      return;
    std::string name = perception::GetProcessName(pid);
    if (!name_filter.empty() && !ContainsCaseInsensitive(name, name_filter) &&
        std::to_string(pid) != name_filter) {
      return;
    }

    size_t unique_bytes = 0;
    size_t shared_bytes = 0;
    size_t creation_ts = 0;
    size_t services = 0;
    uint8 cpu_pcts[kMaxCpuCores] = {};
    perception::GetProcessHealthMetrics(pid, unique_bytes, shared_bytes,
                                        creation_ts, services, cpu_pcts,
                                        kMaxCpuCores);
    int total_cpu = 0;
    for (size_t c = 0; c < kMaxCpuCores; ++c)
      total_cpu += cpu_pcts[c];

    JSValue item = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, item, "pid",
                      JS_NewInt64(ctx, static_cast<int64_t>(pid)));
    JS_SetPropertyStr(ctx, item, "name",
                      JS_NewStringLen(ctx, name.data(), name.size()));
    JS_SetPropertyStr(ctx, item, "uniqueMemoryBytes",
                      JS_NewInt64(ctx, static_cast<int64_t>(unique_bytes)));
    JS_SetPropertyStr(ctx, item, "sharedMemoryBytes",
                      JS_NewInt64(ctx, static_cast<int64_t>(shared_bytes)));
    JS_SetPropertyStr(ctx, item, "services",
                      JS_NewInt64(ctx, static_cast<int64_t>(services)));
    JS_SetPropertyStr(ctx, item, "cpuPercent", JS_NewInt32(ctx, total_cpu));
    JS_SetPropertyUint32(ctx, arr, idx++, item);
  });

  return MakeResolvedPromise(ctx, arr);
}

JSValue JsProcKill(JSContext* ctx, JSValueConst this_val, int argc,
                   JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return MakeRejectedPromise(ctx, "proc.kill() requires a PID or name");

  int64_t numeric_pid = -1;
  bool is_numeric = false;
  if (JS_IsNumber(argv[0])) {
    JS_ToInt64(ctx, &numeric_pid, argv[0]);
    is_numeric = true;
  } else {
    std::string str = JsValueToStdString(ctx, argv[0]);
    if (!str.empty()) {
      char* end = nullptr;
      long long parsed = std::strtoll(str.c_str(), &end, 10);
      if (end && *end == '\0') {
        numeric_pid = parsed;
        is_numeric = true;
      }
    }
  }

  int count = 0;
  if (is_numeric) {
    auto pid = static_cast<perception::ProcessId>(numeric_pid);
    if (perception::DoesProcessExist(pid)) {
      perception::TerminateProcesss(pid);
      count = 1;
    }
    return MakeResolvedPromise(ctx, JS_NewInt32(ctx, count));
  }

  std::string target_name = JsValueToStdString(ctx, argv[0]);
  std::vector<perception::ProcessId> matching_pids;
  perception::ForEachProcess([&](perception::ProcessId pid) {
    if (pid == perception::GetProcessId())
      return;
    std::string proc_name = perception::GetProcessName(pid);
    if (proc_name == target_name ||
        ContainsCaseInsensitive(proc_name, target_name)) {
      matching_pids.push_back(pid);
    }
  });

  for (perception::ProcessId pid : matching_pids) {
    perception::TerminateProcesss(pid);
    ++count;
  }
  return MakeResolvedPromise(ctx, JS_NewInt32(ctx, count));
}

JSValue JsProcWait(JSContext* ctx, JSValueConst this_val, int argc,
                   JSValueConst* argv) {
  (void)this_val;
  if (argc < 1)
    return MakeRejectedPromise(ctx, "proc.wait() requires a PID");
  int64_t pid_val = 0;
  if (JS_ToInt64(ctx, &pid_val, argv[0]) != 0)
    return MakeRejectedPromise(ctx, "Invalid PID");
  JsEngine* engine = GetJsEngine(ctx);
  if (engine && pid_val > 0)
    WaitForPid(*engine, static_cast<perception::ProcessId>(pid_val));
  return MakeResolvedPromise(ctx, JS_UNDEFINED);
}

JSValue JsProcApps(JSContext* ctx, JSValueConst this_val, int argc,
                   JSValueConst* argv) {
  (void)this_val;
  (void)argc;
  (void)argv;
  std::vector<InstalledAppDetails> apps = ScanInstalledApplications();
  JSValue arr = JS_NewArray(ctx);
  for (uint32_t i = 0; i < apps.size(); ++i) {
    JSValue item = JS_NewObject(ctx);
    JS_SetPropertyStr(
        ctx, item, "name",
        JS_NewStringLen(ctx, apps[i].name.data(), apps[i].name.size()));
    JS_SetPropertyStr(
        ctx, item, "path",
        JS_NewStringLen(ctx, apps[i].path.data(), apps[i].path.size()));
    JS_SetPropertyStr(ctx, item, "description",
                      JS_NewStringLen(ctx, apps[i].description.data(),
                                      apps[i].description.size()));
    JS_SetPropertyStr(ctx, item, "terminal",
                      JS_NewBool(ctx, apps[i].terminal));
    JS_SetPropertyUint32(ctx, arr, i, item);
  }
  return MakeResolvedPromise(ctx, arr);
}

JSValue JsProcJobs(JSContext* ctx, JSValueConst this_val, int argc,
                   JSValueConst* argv) {
  (void)this_val;
  (void)argc;
  (void)argv;
  JsEngine* engine = GetJsEngine(ctx);
  JSValue arr = JS_NewArray(ctx);
  if (!engine)
    return arr;
  const auto& jobs = engine->GetBackgroundJobs();
  for (uint32_t i = 0; i < jobs.size(); ++i) {
    ChildProcessData info;
    info.job_id = jobs[i].job_id;
    info.pid = jobs[i].pid;
    info.name = jobs[i].name;
    info.command_line = jobs[i].command_line;
    JS_SetPropertyUint32(ctx, arr, i,
                         CreateChildProcessJsObject(ctx, std::move(info)));
  }
  return arr;
}

}  // namespace

std::vector<std::string> DiscoverInstalledApplications() {
  std::vector<InstalledAppDetails> details = ScanInstalledApplications();
  std::vector<std::string> names;
  names.reserve(details.size());
  for (auto& d : details)
    names.push_back(std::move(d.name));
  return names;
}

bool LaunchTargetDirect(JsEngine& engine, std::string_view target,
                        const std::vector<std::string>& args,
                        bool background_detached, std::string& error_out) {
  std::string resolved = ResolveLaunchTarget(engine.Cwd(), target);
  if (resolved.empty()) {
    error_out = "Empty launch target";
    return false;
  }

  perception::LoadApplicationRequest req;
  req.name = resolved;
  req.arguments = args;
  req.create_as_child = false;
  if (!background_detached) {
    req.stdin_pipe = perception::GetFileDescriptorPipe(kStdinFd);
    req.stdout_pipe = perception::GetFileDescriptorPipe(kStdoutFd);
    req.stderr_pipe = perception::GetFileDescriptorPipe(kStderrFd);
  }

  auto status_or_resp =
      perception::GetService<perception::Loader>().LaunchApplication(req);
  if (!status_or_resp) {
    error_out = "Failed to launch \"" + std::string(target) + "\"";
    return false;
  }

  if (!background_detached)
    WaitForPid(engine, status_or_resp->process);
  return true;
}

void RegisterProcModule(JSContext* ctx, JsEngine& engine) {
  (void)engine;
  JSRuntime* rt = JS_GetRuntime(ctx);

  if (g_pipeline_class_id == 0)
    JS_NewClassID(&g_pipeline_class_id);
  if (!JS_IsRegisteredClass(rt, g_pipeline_class_id)) {
    JSClassDef pipeline_def = {};
    pipeline_def.class_name = "Pipeline";
    pipeline_def.finalizer = JsPipelineFinalizer;
    JS_NewClass(rt, g_pipeline_class_id, &pipeline_def);
  }

  JSValue pipeline_proto = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, pipeline_proto, "pipe",
                    JS_NewCFunction(ctx, JsPipelinePipe, "pipe", 1));
  JS_SetPropertyStr(ctx, pipeline_proto, "out",
                    JS_NewCFunction(ctx, JsPipelineOut, "out", 2));
  JS_SetPropertyStr(ctx, pipeline_proto, "err",
                    JS_NewCFunction(ctx, JsPipelineErr, "err", 2));
  JS_SetPropertyStr(ctx, pipeline_proto, "errToOut",
                    JS_NewCFunction(ctx, JsPipelineErrToOut, "errToOut", 0));
  JS_SetPropertyStr(ctx, pipeline_proto, "nullOut",
                    JS_NewCFunction(ctx, JsPipelineNullOut, "nullOut", 0));
  JS_SetPropertyStr(ctx, pipeline_proto, "nullErr",
                    JS_NewCFunction(ctx, JsPipelineNullErr, "nullErr", 0));
  JS_SetPropertyStr(ctx, pipeline_proto, "tee",
                    JS_NewCFunction(ctx, JsPipelineTee, "tee", 2));
  JS_SetPropertyStr(ctx, pipeline_proto, "text",
                    JS_NewCFunction(ctx, JsPipelineText, "text", 1));
  JS_SetPropertyStr(ctx, pipeline_proto, "lines",
                    JS_NewCFunction(ctx, JsPipelineLines, "lines", 0));
  JS_SetPropertyStr(ctx, pipeline_proto, "json",
                    JS_NewCFunction(ctx, JsPipelineJson, "json", 0));
  JS_SetPropertyStr(ctx, pipeline_proto, "bytes",
                    JS_NewCFunction(ctx, JsPipelineBytes, "bytes", 0));
  JS_SetPropertyStr(ctx, pipeline_proto, "bg",
                    JS_NewCFunction(ctx, JsPipelineBg, "bg", 0));
  JS_SetPropertyStr(ctx, pipeline_proto, "kill",
                    JS_NewCFunction(ctx, JsPipelineKill, "kill", 0));
  JS_SetPropertyStr(ctx, pipeline_proto, "then",
                    JS_NewCFunction(ctx, JsPipelineThen, "then", 2));
  JS_SetPropertyStr(ctx, pipeline_proto, "catch",
                    JS_NewCFunction(ctx, JsPipelineCatch, "catch", 1));
  JS_SetPropertyStr(ctx, pipeline_proto, "finally",
                    JS_NewCFunction(ctx, JsPipelineFinally, "finally", 1));
  JS_SetClassProto(ctx, g_pipeline_class_id, pipeline_proto);

  if (g_child_process_class_id == 0)
    JS_NewClassID(&g_child_process_class_id);
  if (!JS_IsRegisteredClass(rt, g_child_process_class_id)) {
    JSClassDef child_def = {};
    child_def.class_name = "ChildProcess";
    child_def.finalizer = JsChildProcessFinalizer;
    JS_NewClass(rt, g_child_process_class_id, &child_def);
  }

  JSValue child_proto = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, child_proto, "running",
                    JS_NewCFunction(ctx, JsChildProcessRunning, "running", 0));
  JS_SetPropertyStr(ctx, child_proto, "wait",
                    JS_NewCFunction(ctx, JsChildProcessWait, "wait", 0));
  JS_SetPropertyStr(ctx, child_proto, "kill",
                    JS_NewCFunction(ctx, JsChildProcessKill, "kill", 0));
  JS_SetPropertyStr(ctx, child_proto, "toJSON",
                    JS_NewCFunction(ctx, JsChildProcessToJson, "toJSON", 0));
  JS_SetClassProto(ctx, g_child_process_class_id, child_proto);

  JSValue global = JS_GetGlobalObject(ctx);
  JS_SetPropertyStr(ctx, global, "run",
                    JS_NewCFunction(ctx, JsRun, "run", 1));

  JSValue pipe_fn = JS_NewCFunction(ctx, JsPipe, "pipe", 1);
  JS_SetPropertyStr(ctx, pipe_fn, "from",
                    JS_NewCFunction(ctx, JsPipeFrom, "from", 1));
  JS_SetPropertyStr(ctx, pipe_fn, "file",
                    JS_NewCFunction(ctx, JsPipeFile, "file", 1));
  JS_SetPropertyStr(ctx, pipe_fn, "seq",
                    JS_NewCFunction(ctx, JsPipeSeq, "seq", 1));
  JS_SetPropertyStr(ctx, pipe_fn, "merge",
                    JS_NewCFunction(ctx, JsPipeMerge, "merge", 1));
  JS_SetPropertyStr(ctx, global, "pipe", pipe_fn);

  JSValue proc_obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, proc_obj, "ps",
                    JS_NewCFunction(ctx, JsProcPs, "ps", 1));
  JS_SetPropertyStr(ctx, proc_obj, "kill",
                    JS_NewCFunction(ctx, JsProcKill, "kill", 1));
  JS_SetPropertyStr(ctx, proc_obj, "wait",
                    JS_NewCFunction(ctx, JsProcWait, "wait", 1));
  JS_SetPropertyStr(ctx, proc_obj, "apps",
                    JS_NewCFunction(ctx, JsProcApps, "apps", 0));
  JS_SetPropertyStr(ctx, proc_obj, "jobs",
                    JS_NewCFunction(ctx, JsProcJobs, "jobs", 0));
  JS_SetPropertyStr(
      ctx, proc_obj, "pid",
      JS_NewInt64(ctx, static_cast<int64_t>(perception::GetProcessId())));
  JS_SetPropertyStr(ctx, global, "proc", proc_obj);

  JS_FreeValue(ctx, global);
}

}  // namespace module
