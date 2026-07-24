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

#include "window_manager.h"

#include <iostream>

#include "perception/launcher.h"
#include "perception/loader.h"
#include "perception/registry.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/ui/color_space.h"
#include "perception/ui/point.h"
#include "perception/ui/rectangle.h"
#include "screen.h"
#include "status.h"
#include "window.h"

namespace {

std::string current_serialized_color_space;
sk_sp<SkColorSpace> current_color_space;
float current_scale = 1.0f;
std::vector<::perception::window::WindowManagerEnvironmentListener::Client>
    environment_listeners;

}  // namespace

using ::perception::FindFirstInstanceOfService;
using ::perception::GetService;
using ::perception::Launcher;
using ::perception::ProcessId;
using ::perception::ui::Point;
using ::perception::ui::Rectangle;
using ::perception::window::BaseWindow;
using ::perception::window::CreateWindowRequest;
using ::perception::window::CreateWindowResponse;
using ::perception::window::DisplayEnvironment;
using ::perception::window::GetEnvironmentResponse;
using ::perception::window::InvalidateWindowParameters;
using ::perception::window::SetWindowTextureParameters;
using ::perception::window::SetWindowTitleParameters;
using ::perception::window::Size;

StatusOr<CreateWindowResponse> WindowManager::CreateWindow(
    const CreateWindowRequest& request, ProcessId sender) {
  ASSIGN_OR_RETURN(auto window, Window::CreateWindow(request));

  if (!window) {
    std::cout << "Internal error creating new window " << request.title
              << std::endl;
    return Status::INTERNAL_ERROR;
  }

  auto content_size = window->GetContentSize();

  CreateWindowResponse response;
  response.window_size = {content_size.width, content_size.height};
  return response;
}

Status WindowManager::CloseWindow(const BaseWindow::Client& window_listener,
                                  ::perception::ProcessId sender) {
  auto window = GetWindowWithListener(window_listener);
  if (!window) return Status::INVALID_ARGUMENT;

  window->Close();
  return Status::OK;
}

Status WindowManager::SetWindowTexture(
    const SetWindowTextureParameters& parameters,
    ::perception::ProcessId sender) {
  auto window = GetWindowWithListener(parameters.window);
  if (!window) return Status::INVALID_ARGUMENT;

  window->SetTextureId(parameters.texture.id);
  return Status::OK;
}

Status WindowManager::SetWindowTitle(const SetWindowTitleParameters& parameters,
                                     ::perception::ProcessId sender) {
  auto window = GetWindowWithListener(parameters.window);
  if (!window || sender != parameters.window.ServerProcessId())
    return Status::INVALID_ARGUMENT;

  window->SetTitle(parameters.title);
  return Status::OK;
}

Status WindowManager::SystemButtonPushed() {
  if (Window::ExitFullScreenOrMouseCapture()) return Status::OK;

  auto launcher = FindFirstInstanceOfService<Launcher>();
  if (launcher) launcher->ShowLauncher(nullptr);
  return Status::OK;
}

Status WindowManager::InvalidateWindow(
    const InvalidateWindowParameters& parameters,
    ::perception::ProcessId sender) {
  auto window = GetWindowWithListener(parameters.window);
  if (!window) return Status::INVALID_ARGUMENT;

  window->InvalidateLocalArea(
      Rectangle::FromMinMaxPoints(Point{parameters.left, parameters.top},
                                  Point{parameters.right, parameters.bottom}));
  return Status::OK;
}

StatusOr<Size> WindowManager::GetMaximumWindowSize() {
  auto screen_size = GetScreenSize();
  return Size(screen_size.width, screen_size.height);
}

StatusOr<DisplayEnvironment> WindowManager::GetDisplayEnvironment() {
  return Status::UNIMPLEMENTED;
}

Status WindowManager::StartDraggingWindow(
    const BaseWindow::Client& window_listener, ::perception::ProcessId sender) {
  auto window = GetWindowWithListener(window_listener);
  if (!window || sender != window_listener.ServerProcessId())
    return Status::INVALID_ARGUMENT;

  window->StartDragging();
  return Status::OK;
}

Status WindowManager::FocusWindow(const BaseWindow::Client& window_listener,
                                  ::perception::ProcessId sender) {
  auto window = GetWindowWithListener(window_listener);
  if (!window || sender != window_listener.ServerProcessId())
    return Status::INVALID_ARGUMENT;

  window->Focus();
  return Status::OK;
}

Status WindowManager::SetWindowSize(
    const ::perception::window::SetWindowSizeParameters& parameters,
    ::perception::ProcessId sender) {
  auto window = GetWindowWithListener(parameters.window);
  if (!window || sender != parameters.window.ServerProcessId())
    return Status::INVALID_ARGUMENT;

  window->SetSize(parameters.size);
  return Status::OK;
}

Status WindowManager::SetWindowCursor(
    const ::perception::window::SetWindowCursorParameters& parameters,
    ::perception::ProcessId sender) {
  auto window = GetWindowWithListener(parameters.window);
  if (!window || sender != parameters.window.ServerProcessId())
    return Status::INVALID_ARGUMENT;

  window->SetCursor(parameters.cursor);
  return Status::OK;
}

StatusOr<::perception::window::GetDisplayBoundsResponse>
WindowManager::GetDisplayBounds() {
  auto screen_size = GetScreenSize();
  ::perception::window::GetDisplayBoundsResponse response;
  ::perception::window::Rectangle bounds(0, 0, screen_size.width,
                                         screen_size.height);
  response.bounds.push_back(bounds);
  return response;
}

Status WindowManager::SetWindowMinimumSize(
    const ::perception::window::SetWindowMinimumSizeRequest& parameters,
    ::perception::ProcessId sender) {
  auto window = GetWindowWithListener(parameters.window);
  if (!window || sender != parameters.window.ServerProcessId())
    return Status::INVALID_ARGUMENT;

  window->SetMinimumSize(parameters.size);
  return Status::OK;
}

Status WindowManager::SetWindowMaximumSize(
    const ::perception::window::SetWindowMaximumSizeRequest& parameters,
    ::perception::ProcessId sender) {
  auto window = GetWindowWithListener(parameters.window);
  if (!window || sender != parameters.window.ServerProcessId())
    return Status::INVALID_ARGUMENT;

  window->SetMaximumSize(parameters.size);
  return Status::OK;
}

Status WindowManager::SetWindowCaptureMouse(
    const ::perception::window::SetWindowCaptureMouseRequest& parameters,
    ::perception::ProcessId sender) {
  auto window = GetWindowWithListener(parameters.window);
  if (!window || sender != parameters.window.ServerProcessId())
    return Status::INVALID_ARGUMENT;

  window->SetCaptureMouse(parameters.capture);
  return Status::OK;
}

StatusOr<GetEnvironmentResponse> WindowManager::GetEnvironment() {
  GetEnvironmentResponse response;
  response.color_space = GetSerializedColorSpace();
  response.scale = GetScale();
  return response;
}

void WindowManager::InitializeEnvironment() {
  ::perception::NotifyOnEachNewServiceInstance<
      ::perception::window::WindowManagerEnvironmentListener>(
      [](::perception::window::WindowManagerEnvironmentListener::Client
             client) {
        environment_listeners.push_back(client);
        ::perception::NotifyWhenServiceDisappears(client, [client]() {
          for (auto it = environment_listeners.begin();
               it != environment_listeners.end(); ++it) {
            if (it->ServerProcessId() == client.ServerProcessId() &&
                it->ServiceId() == client.ServiceId()) {
              environment_listeners.erase(it);
              break;
            }
          }
        });
      });

  UpdateEnvironmentFromRegistry();
}

void WindowManager::UpdateEnvironmentFromRegistry() {
  ::perception::DeferAfterEvents([]() {
    int transfer_fn_val = 0;
    int gamut_val = 0;

    auto tr_or = ::perception::GetRegistryValue(
        ::perception::RegistryCorpus::APPLICATIONS, "Window Manager",
        "colorSpaceTransferFn");
    if (tr_or.Ok() &&
        tr_or->GetType() == ::perception::serialization::Value::Type::INTEGER) {
      transfer_fn_val = tr_or->IntegerValue().value_or(0);
    }

    auto gamut_or = ::perception::GetRegistryValue(
        ::perception::RegistryCorpus::APPLICATIONS, "Window Manager",
        "colorSpaceGamut");
    if (gamut_or.Ok() &&
        gamut_or->GetType() ==
            ::perception::serialization::Value::Type::INTEGER) {
      gamut_val = gamut_or->IntegerValue().value_or(0);
    }

    float new_scale = 1.0f;
    auto scale_or = ::perception::GetRegistryValue(
        ::perception::RegistryCorpus::APPLICATIONS, "Window Manager", "scale");
    if (scale_or.Ok()) {
      if (scale_or->GetType() ==
          ::perception::serialization::Value::Type::FLOAT) {
        float val = static_cast<float>(scale_or->FloatValue().value_or(1.0));
        new_scale = (val > 10.0f) ? (val / 100.0f) : val;
      } else if (scale_or->GetType() ==
                 ::perception::serialization::Value::Type::INTEGER) {
        int64 val = scale_or->IntegerValue().value_or(100);
        new_scale = (val > 10) ? (static_cast<float>(val) / 100.0f)
                               : static_cast<float>(val);
      }
    }
    if (new_scale < 0.5f) new_scale = 0.5f;
    if (new_scale > 2.5f) new_scale = 2.5f;

    auto transfer_fn =
        static_cast<::perception::ui::ColorSpaceTransferFn>(transfer_fn_val);
    auto gamut = static_cast<::perception::ui::ColorSpaceGamut>(gamut_val);

#ifdef TEST
    std::string new_serialized = "";
#else
    auto new_color_space =
        ::perception::ui::CreateSkColorSpace(transfer_fn, gamut);
    std::string new_serialized =
        ::perception::ui::SerializeColorSpace(new_color_space.get());
#endif

    if (new_serialized != current_serialized_color_space ||
        new_scale != current_scale) {
      current_serialized_color_space = new_serialized;

      if (new_scale != current_scale) {
        current_scale = new_scale;
        Window::OnScaleChanged();
        InitializeWindowButtons();
      }

      ::perception::window::WindowManagerEnvironmentChangedNotification
          notification;
      notification.color_space = current_serialized_color_space;
      notification.scale = current_scale;

      for (auto& listener : environment_listeners) {
        listener.WindowManagerEnvironmentChanged(notification);
      }
    }
  });
}

std::string WindowManager::GetSerializedColorSpace() {
#ifdef TEST
  return current_serialized_color_space;
#else
  if (current_serialized_color_space.empty()) {
    auto default_cs = ::perception::ui::CreateSkColorSpace(
        ::perception::ui::ColorSpaceTransferFn::SRGB,
        ::perception::ui::ColorSpaceGamut::SRGB);
    current_serialized_color_space =
        ::perception::ui::SerializeColorSpace(default_cs.get());
    current_color_space = default_cs;
  }
  return current_serialized_color_space;
#endif
}

float WindowManager::GetScale() { return current_scale; }
