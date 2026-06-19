// Copyright 2024 Google LLC
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

#include <filesystem>
#include <iostream>

#include "elf_file_cache.h"
#include "file.h"
#include "loader.h"
#include "loader_server.h"
#include "multiboot.h"
#include "perception/fibers.h"
#include "perception/launcher.h"
#include "perception/processes.h"
#include "perception/registry.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/storage_manager.h"
#include "perception/time.h"

using ::perception::GetService;
using ::perception::HandOverControl;
using ::perception::StorageManager;

std::vector<std::shared_ptr<ElfFile>> preloaded_programs;

void PreloadProgram(std::string_view name) {
  auto path = GetPathToFile(name);
  if (!path) return;
  auto elf_file = LoadOrIncrementElfFile(*path);
  if (elf_file) preloaded_programs.push_back(elf_file);
}

void LoadInitialPrograms() {
  auto status_or_value = ::perception::GetRegistryValue(
      ::perception::RegistryCorpus::APPLICATIONS, "Loader", "launchOnBoot");
  if (!status_or_value.Ok()) return;

  const auto& value = *status_or_value;
  if (value.GetType() != ::perception::serialization::Value::Type::ARRAY) {
    std::cout << "launchOnBoot is not an array." << std::endl;
    return;
  }

  const auto* array = value.ArrayValue();
  if (array == nullptr) return;

  // 1. Preload all programs sequentially.
  for (const auto& item : *array) {
    if (item.GetType() == ::perception::serialization::Value::Type::STRING) {
      auto name_opt = item.StringValue();
      if (name_opt.has_value()) PreloadProgram(*name_opt);
    }
  }

  // 2. Launch them sequentially.
  auto pid = ::perception::GetProcessId();
  for (const auto& item : *array) {
    if (item.GetType() == ::perception::serialization::Value::Type::STRING) {
      auto name_opt = item.StringValue();
      if (name_opt.has_value()) {
        auto status_or_pid = LoadProgram(pid, *name_opt);
        if (!status_or_pid.Ok()) {
          std::cout << "Loader: Failed to launch: " << *name_opt << std::endl;
        }
      }
    }
  }
}

int main(int argc, char* argv[]) {
  // Load the multiboot modules first.
  LoadMultibootModules();

  // Create the loader server that listens to requests to launch executables.
  auto loader_server = std::make_unique<LoaderServer>();

  ::perception::Defer([]() {
    auto storage_manager = GetService<StorageManager>();
    auto loading_fiber = ::perception::GetCurrentlyExecutingFiber();

    class LoaderMountListener
        : public ::perception::FileSystemMountListener::Server {
     public:
      LoaderMountListener(::perception::Fiber* fiber_to_wake)
          : fiber_to_wake_(fiber_to_wake) {}
      virtual ::perception::Status FileSystemMounted(
          const ::perception::FileSystemMountEvent& event,
          ::perception::ProcessId sender) override {
        fiber_to_wake_->WakeUp();
        return ::perception::Status::OK;
      }

     private:
      ::perception::Fiber* fiber_to_wake_;
    };

    auto mount_listener = std::make_unique<LoaderMountListener>(loading_fiber);
    storage_manager.ListenForMounts(*mount_listener);

    while (true) {
      bool applications_ready = false;
      std::error_code ec;
      auto iter = std::filesystem::directory_iterator("/Applications", ec);
      if (!ec) {
        int count = 0;
        for (const auto& entry : iter) count++;

        if (count > 0) applications_ready = true;

      } else {
        std::cout << "Loader check loop: directory_iterator failed: "
                  << ec.message() << " (code " << ec.value() << ")"
                  << std::endl;
      }

      if (applications_ready) break;

      ::perception::Sleep();
    }

    storage_manager.StopListeningForMounts(*mount_listener);
    PopulateFilePathsCache();
    LoadInitialPrograms();
  });

  HandOverControl();
  return 0;
}