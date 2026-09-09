// Copyright 2021 Google LLC
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

#include "applications.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"
#include "perception/fibers.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/storage_manager.h"

using json = ::nlohmann::json;

namespace {

std::vector<Application> applications;

std::optional<Application> MaybeLoadApplication(std::string_view path) {
  try {
    std::ifstream launcher_metadata_file(std::string(path) + "/launcher.json");
    if (!launcher_metadata_file.is_open()) {
      // The application is missing a launcher.json so don't show it in the
      // launcher.
      return std::nullopt;
    }
    json data = json::parse(launcher_metadata_file);
    Application application;
    auto name_itr = data.find("name");
    if (name_itr != data.end() && name_itr->is_string())
      name_itr->get_to(application.name);

    if (application.name.empty())
      application.name = std::filesystem::path(path).filename();

    application.path = std::string(path) + "/" +
                       std::string(std::filesystem::path(path).filename()) +
                       ".app";
    auto description_itr = data.find("description");
    if (description_itr != data.end() && description_itr->is_string())
      description_itr->get_to(application.description);

    std::error_code ec_exists;
    std::string png_path = std::string(path) + "/icon.png";
    if (std::filesystem::exists(png_path, ec_exists) && !ec_exists)
      application.icon = ::perception::ui::Image::LoadImage(png_path);

    launcher_metadata_file.close();
    return application;
  } catch (...) {
    std::cout << "Unhandled exception" << std::endl;
    return std::nullopt;
  }
}

enum class InitializationState { UNINITIALIZED, INITIALIZING, INITIALIZED };
InitializationState applications_state = InitializationState::UNINITIALIZED;

std::vector<std::function<void(const Application&)>>
    application_found_callbacks;
std::vector<std::function<void()>> scan_finished_callbacks;
std::vector<std::function<void()>> applications_changed_callbacks;

bool ContainsApplication(std::string_view path) {
  for (const auto& app : applications)
    if (app.path == path) return true;
  return false;
}

void OnApplicationFound(const Application& application) {
  std::error_code ec;
  if (!std::filesystem::exists(application.path, ec) || ec) return;
  if (ContainsApplication(application.path)) return;

  applications.push_back(application);
  for (const auto& callback : application_found_callbacks)
    callback(application);
}

void OnScanFinished() {
  applications_state = InitializationState::INITIALIZED;
  for (const auto& callback : scan_finished_callbacks) callback();
}

void ScanMountPointInBackground(std::string mount_point) {
  std::string_view mount = mount_point;
  while (!mount.empty() && mount.front() == '/') mount.remove_prefix(1);
  while (!mount.empty() && mount.back() == '/') mount.remove_suffix(1);
  if (mount.empty()) return;

  try {
    std::string app_path = "/" + std::string(mount) + "/Applications";
    std::error_code ec;
    if (std::filesystem::exists(app_path, ec) && !ec) {
      for (const auto& app_entry :
           std::filesystem::directory_iterator(app_path, ec)) {
        if (ec) break;
        auto opt_app = MaybeLoadApplication(std::string(app_entry.path()));
        if (opt_app) {
          ::perception::Defer([app = std::move(*opt_app)]() {
            OnApplicationFound(app);
          });
        }
      }
    }
  } catch (...) {
  }
}

void OnMountPointMounted(std::string_view mount_point) {
  if (applications_state != InitializationState::INITIALIZED) return;

  std::string mount_str(mount_point);
  ::perception::DeferInParallel([mount_str = std::move(mount_str)]() {
    ScanMountPointInBackground(mount_str);
  });
}

void OnMountPointUnmounted(std::string_view mount_point) {
  std::string_view mount = mount_point;
  while (!mount.empty() && mount.front() == '/') mount.remove_prefix(1);
  while (!mount.empty() && mount.back() == '/') mount.remove_suffix(1);
  if (mount.empty()) return;

  std::string prefix = "/" + std::string(mount) + "/";

  auto it = std::remove_if(applications.begin(), applications.end(),
                           [&prefix](const Application& app) {
                             return app.path.starts_with(prefix);
                           });

  if (it != applications.end()) {
    applications.erase(it, applications.end());
    for (const auto& callback : applications_changed_callbacks) callback();
  }
}

class LauncherMountListener
    : public ::perception::FileSystemMountListener::Server {
 public:
  virtual Status FileSystemMounted(
      const ::perception::FileSystemMountEvent& event) override {
    std::string mount_point = event.mount_point;
    ::perception::Defer([mount_point = std::move(mount_point)]() {
      OnMountPointMounted(mount_point);
    });
    return Status::OK;
  }

  virtual Status FileSystemUnmounted(
      const ::perception::FileSystemMountEvent& event) override {
    std::string mount_point = event.mount_point;
    ::perception::Defer([mount_point = std::move(mount_point)]() {
      OnMountPointUnmounted(mount_point);
    });
    return Status::OK;
  }
};

std::shared_ptr<LauncherMountListener> mount_listener;

void DoScanInBackground() {
  try {
    for (const auto& root_entry : std::filesystem::directory_iterator("/")) {
      std::string app_path = std::string(root_entry.path()) + "/Applications";
      if (std::filesystem::exists(app_path)) {
        for (const auto& app_entry :
             std::filesystem::directory_iterator(app_path)) {
          auto opt_app = MaybeLoadApplication(std::string(app_entry.path()));
          if (opt_app) {
            ::perception::Defer(
                [app = std::move(*opt_app)]() { OnApplicationFound(app); });
          }
        }
      }
    }
  } catch (...) {
  }

  ::perception::Defer([]() { OnScanFinished(); });
}

}  // namespace

void InitializeApplicationsMountListener() {
  static bool initialized = false;
  if (initialized) return;
  initialized = true;

  mount_listener = std::make_shared<LauncherMountListener>();
  ::perception::NotifyOnEachNewServiceInstance<::perception::StorageManager>(
      [](::perception::StorageManager::Client storage_manager) {
        storage_manager.ListenForMounts(*mount_listener);
      });
}

void ScanForApplications() {
  InitializeApplicationsMountListener();

  if (applications_state != InitializationState::UNINITIALIZED) return;

  applications_state = InitializationState::INITIALIZING;
  ::perception::DeferInParallel([]() { DoScanInBackground(); });
}

const std::vector<Application>& GetApplications() { return applications; }

void RegisterApplicationFoundCallback(
    std::function<void(const Application&)> callback) {
  application_found_callbacks.push_back(callback);
}

void RegisterScanFinishedCallback(std::function<void()> callback) {
  scan_finished_callbacks.push_back(callback);
}

void RegisterApplicationsChangedCallback(std::function<void()> callback) {
  applications_changed_callbacks.push_back(callback);
}
