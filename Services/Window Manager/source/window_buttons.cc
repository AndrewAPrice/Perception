// Copyright 2025 Google LLC
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
#include "window_buttons.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "fpng.h"
#include "perception/devices/graphics_device.h"
#include "perception/services.h"
#include "perception/ui/point.h"
#include "perception/ui/size.h"
#include "pvpngreader.h"
#include "status.h"

using ::perception::GetService;
using ::perception::Status;
using ::perception::devices::GraphicsDevice;
using ::perception::ui::Point;
using ::perception::ui::Size;
namespace graphics = ::perception::devices::graphics;

namespace {

int window_buttons_texture_id;

constexpr std::string_view kWindowsButtonPath =
    "/Applications/Window Manager/window buttons.png";
constexpr int kButtonPanelWidth = 60;
constexpr int kButtonPanelHeight = 24;
constexpr int kButtonSize = 18;

constexpr int kExpectedTextureHeight = kButtonPanelHeight * 7;
constexpr int kButtonPanelWidthWithoutToggle = kButtonPanelWidth - kButtonSize;

constexpr int kFirstButtonThreshold =
    (kButtonPanelHeight - kButtonSize) / 2 + kButtonSize;
constexpr int kSecondButtonThreshold = kFirstButtonThreshold + kButtonSize;

constexpr int kPaddingDistance = 3;

StatusOr<std::vector<char>> LoadWindowButtonsFile() {
  // Open the file in binary mode and position the file pointer at the end.
  std::ifstream file(kWindowsButtonPath, std::ios::binary);
  if (!file || !file.is_open()) {
    std::cout << "Cannot open " << kWindowsButtonPath << std::endl;
    return Status::FILE_NOT_FOUND;
  }

  // Read the entire file into a vector.
  return std::vector<char>((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
}

struct VoidPtrDeleter {
  void operator()(void* ptr) const { free(ptr); }
};

int WindowButtonTextureVariant(
    bool is_resizable, const std::optional<WindowButton>& selected_button) {
  if (selected_button) {
    switch (*selected_button) {
      case WindowButton::Close:
        return is_resizable ? 3 : 6;
      case WindowButton::Minimize:
        return is_resizable ? 2 : 5;
      case WindowButton::ToggleFullScreen:
        return 1;
    }
  }

  // No selected button.
  return is_resizable ? 0 : 4;
}

}  // namespace

Status InitializeWindowButtons() {
  // Decode the image data.
  ASSIGN_OR_RETURN(auto file_buffer, LoadWindowButtonsFile());

  std::vector<uint8_t> pixel_data;
  uint32_t width, height, channels_in_file;

  int status =
      fpng::fpng_decode_memory(file_buffer.data(), file_buffer.size(),
                               pixel_data, width, height, channels_in_file,
                               /*desired_channels=*/4);
  /**
  int count = 0;
  for (uint8_t c : file_buffer) {
    if (count % 16 == 0) {
      printf("\n%08x", count);
    }
    int val = (int)c;
    printf(" %02x", val);
    count++;
  }
    */

  std::unique_ptr<void, VoidPtrDeleter> raw_data(
      pv_png::load_png(file_buffer.data(), file_buffer.size(),
                       /*desired_chans=*/4, width, height, channels_in_file),
      VoidPtrDeleter());
  if (!raw_data) {
    std::cout << "Can't decode PNG or FPNG: " << kWindowsButtonPath
              << std::endl;
    return Status::INTERNAL_ERROR;
  }

  size_t pixel_data_size =
      static_cast<size_t>(width * height * channels_in_file);

  if (width != kButtonPanelWidth || height != kExpectedTextureHeight) {
    std::cout << "Expected the size of " << kWindowsButtonPath << " to be "
              << kButtonPanelWidth << "x" << kExpectedTextureHeight
              << " but it was " << width << "x" << height << "." << std::endl;
    return Status::INTERNAL_ERROR;
  }

  // Load the pixel data into a texture.
  graphics::CreateTextureRequest request;
  request.size = graphics::Size(kButtonPanelWidth, kExpectedTextureHeight);

  ASSIGN_OR_RETURN(auto response,
                   GetService<GraphicsDevice>().CreateTexture(request));

  window_buttons_texture_id = response.texture.id;
  response.pixel_buffer->Apply([&](void* data, size_t size) {
    memcpy(data, raw_data.get(), std::min(size, pixel_data_size));
  });
  return Status::OK;
}

int WindowButtonsTextureId() { return window_buttons_texture_id; }

Size WindowButtonSize(bool is_resizable) {
  return Size{.width = static_cast<float>(is_resizable
                                              ? kButtonPanelWidth
                                              : kButtonPanelWidthWithoutToggle),
              .height = static_cast<float>(kButtonPanelHeight)};
}

Point WindowButtonTextureOffset(
    bool is_resizable, const std::optional<WindowButton>& selected_button) {
  return Point{.x = is_resizable ? 0.0f : static_cast<float>(kButtonSize),
               .y = static_cast<float>(
                   WindowButtonTextureVariant(is_resizable, selected_button) *
                   kButtonPanelHeight)};
}

WindowButton GetWindowButtonAtPoint(int x, bool is_resizable) {
  if (is_resizable) {
    if (x >= kSecondButtonThreshold) {
      return WindowButton::Close;
    } else if (x >= kFirstButtonThreshold) {
      return WindowButton::Minimize;
    } else {
      return WindowButton::ToggleFullScreen;
    }
  } else {
    if (x >= kFirstButtonThreshold) {
      return WindowButton::Close;
    } else {
      return WindowButton::Minimize;
    }
  }
}
