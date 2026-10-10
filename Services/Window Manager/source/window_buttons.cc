#include "window_buttons.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>

#ifndef TEST
#include "fpng.h"
#include "pvpngreader.h"
#endif
#include "perception/ui/point.h"
#include "perception/ui/size.h"
#include "status.h"
#include "window_manager.h"

using ::perception::GetService;
using ::perception::devices::GraphicsDevice;
using ::perception::ui::Point;
using ::perception::ui::Size;
namespace graphics = ::perception::devices::graphics;

namespace {

int window_buttons_texture_id = 0;

// Path to the window buttons sprite sheet asset.
constexpr std::string_view kWindowsButtonPath =
    "/Applications/Window Manager/window buttons.png";

// Unscaled width of the full button panel in pixels.
constexpr int kButtonPanelWidth = 60;

// Unscaled height of a single button panel row in pixels.
constexpr int kButtonPanelHeight = 24;

// Unscaled width and height of an individual window button in pixels.
constexpr int kButtonSize = 18;

// Total expected height of the sprite sheet containing all 7 variants.
constexpr int kExpectedTextureHeight = kButtonPanelHeight * 7;

// Unscaled width of the button panel when the fullscreen toggle is hidden.
constexpr int kButtonPanelWidthWithoutToggle = kButtonPanelWidth - kButtonSize;

// Horizontal threshold separating the first and second buttons.
constexpr int kFirstButtonThreshold =
    (kButtonPanelHeight - kButtonSize) / 2 + kButtonSize;

// Horizontal threshold separating the second and third buttons.
constexpr int kSecondButtonThreshold = kFirstButtonThreshold + kButtonSize;

// Padding distance around window buttons in pixels.
constexpr int kPaddingDistance = 3;

// Cached unscaled RGBA pixels of the window buttons sprite sheet.
std::vector<uint32_t> unscaled_button_pixels;

StatusOr<std::vector<char>> LoadWindowButtonsFile() {
  // Open the file in binary mode and position the file pointer at the end.
  std::ifstream file(std::string(kWindowsButtonPath),
                     std::ios::binary | std::ios::ate);
  if (!file || !file.is_open()) {
    std::cout << "Cannot open " << kWindowsButtonPath << std::endl;
    return Status::FILE_NOT_FOUND;
  }

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<char> buffer(size);
  if (!file.read(buffer.data(), size)) {
    std::cout << "Cannot read " << kWindowsButtonPath << std::endl;
    return Status::INTERNAL_ERROR;
  }

  return buffer;
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
      case WindowButton::Debug:
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
#ifndef TEST
  uint32_t width = kButtonPanelWidth;
  uint32_t height = kExpectedTextureHeight;

  if (unscaled_button_pixels.empty()) {
    // Decode the image data once and cache the unscaled pixels.
    ASSIGN_OR_RETURN(auto file_buffer, LoadWindowButtonsFile());

    std::vector<uint8_t> pixel_data;
    uint32_t channels_in_file = 0;

    int status =
        fpng::fpng_decode_memory(file_buffer.data(), file_buffer.size(),
                                 pixel_data, width, height, channels_in_file,
                                 /*desired_channels=*/4);

    std::unique_ptr<void, VoidPtrDeleter> raw_data;
    const uint32_t* decoded_pixels = nullptr;
    if (status == fpng::FPNG_DECODE_SUCCESS) {
      decoded_pixels = reinterpret_cast<const uint32_t*>(pixel_data.data());
    } else {
      raw_data = std::unique_ptr<void, VoidPtrDeleter>(
          pv_png::load_png(file_buffer.data(), file_buffer.size(),
                           /*desired_chans=*/4, width, height,
                           channels_in_file),
          VoidPtrDeleter());
      if (!raw_data) {
        std::cout << "Can't decode PNG or FPNG: " << kWindowsButtonPath
                  << std::endl;
        return Status::INTERNAL_ERROR;
      }
      decoded_pixels = static_cast<const uint32_t*>(raw_data.get());
    }

    if (width != kButtonPanelWidth || height != kExpectedTextureHeight) {
      std::cout << "Expected the size of " << kWindowsButtonPath << " to be "
                << kButtonPanelWidth << "x" << kExpectedTextureHeight
                << " but it was " << width << "x" << height << "." << std::endl;
      return Status::INTERNAL_ERROR;
    }

    unscaled_button_pixels.assign(decoded_pixels,
                                  decoded_pixels + width * height);
  }

  float scale = WindowManager::GetScale();
  uint32_t scaled_width =
      static_cast<uint32_t>(std::round(static_cast<float>(width) * scale));
  uint32_t scaled_height =
      static_cast<uint32_t>(std::round(static_cast<float>(height) * scale));
  if (scaled_width < 1) scaled_width = 1;
  if (scaled_height < 1) scaled_height = 1;

  if (window_buttons_texture_id != 0) {
    GetService<GraphicsDevice>().DestroyTexture(
        graphics::TextureReference(window_buttons_texture_id), [](Status) {});
    window_buttons_texture_id = 0;
  }

  std::vector<uint32_t> scaled_pixels(scaled_width * scaled_height);
  const uint32_t* src_pixels = unscaled_button_pixels.data();

  for (uint32_t y = 0; y < scaled_height; y++) {
    uint32_t src_y = std::min(
        height - 1,
        static_cast<uint32_t>(std::round(static_cast<float>(y) / scale)));
    for (uint32_t x = 0; x < scaled_width; x++) {
      uint32_t src_x = std::min(
          width - 1,
          static_cast<uint32_t>(std::round(static_cast<float>(x) / scale)));
      scaled_pixels[y * scaled_width + x] = src_pixels[src_y * width + src_x];
    }
  }

  // Load the pixel data into a texture.
  graphics::CreateTextureRequest request;
  request.size = graphics::Size(scaled_width, scaled_height);

  ASSIGN_OR_RETURN(auto response,
                   GetService<GraphicsDevice>().CreateTexture(request));

  window_buttons_texture_id = response.texture.id;
  response.pixel_buffer->Apply([&](void* data, size_t size) {
    memcpy(data, scaled_pixels.data(),
           std::min(size, scaled_pixels.size() * sizeof(uint32_t)));
  });
#endif
  return Status::OK;
}

int WindowButtonsTextureId() { return window_buttons_texture_id; }

Size WindowButtonSize(bool is_resizable) {
  float scale = WindowManager::GetScale();
  float unscaled_w = static_cast<float>(
      is_resizable ? kButtonPanelWidth : kButtonPanelWidthWithoutToggle);
  return Size{
      .width = std::round(unscaled_w * scale),
      .height = std::round(static_cast<float>(kButtonPanelHeight) * scale)};
}

Point WindowButtonTextureOffset(
    bool is_resizable, const std::optional<WindowButton>& selected_button) {
  float scale = WindowManager::GetScale();
  float unscaled_x = is_resizable ? 0.0f : static_cast<float>(kButtonSize);
  float unscaled_y = static_cast<float>(
      WindowButtonTextureVariant(is_resizable, selected_button) *
      kButtonPanelHeight);
  return Point{.x = std::round(unscaled_x * scale),
               .y = std::round(unscaled_y * scale)};
}

WindowButton GetWindowButtonAtPoint(int x, bool is_resizable) {
  float scale = WindowManager::GetScale();
  int unscaled_x = static_cast<int>(std::round(static_cast<float>(x) / scale));
  if (!is_resizable) unscaled_x += kButtonSize;

  if (unscaled_x >= kSecondButtonThreshold) return WindowButton::Close;
  if (unscaled_x >= kFirstButtonThreshold) return WindowButton::Debug;
  return WindowButton::ToggleFullScreen;
}
