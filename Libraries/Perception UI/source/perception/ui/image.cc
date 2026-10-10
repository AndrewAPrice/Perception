// Copyright 2023 Google LLC
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

#include "perception/ui/image.h"

#include <math.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkStream.h"
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGRenderContext.h"
#include "perception/ui/size.h"

namespace perception {
namespace ui {
namespace {

sk_sp<SkData> LoadFileToSkData(std::string_view path) {
  return SkData::MakeFromFileName(std::string(path).c_str());
}

class RasterImage : public Image {
 public:
  RasterImage(sk_sp<SkImage> image) : sk_image_(image) {}
  virtual ~RasterImage() {}

  SkImage* GetSkImage(const Size& size, bool& matches_dimensions) override {
    matches_dimensions =
        std::abs(size.width - (float)sk_image_->width()) < 1.0f &&
        std::abs(size.height - (float)sk_image_->height()) < 1.0f;
    return sk_image_.get();
  }

  virtual SkSVGDOM* GetSkSVGDOM(const Size& size) override { return nullptr; }

  Size GetSize(const Size& container_size) override {
    return {.width = (float)sk_image_->width(),
            .height = (float)sk_image_->height()};
  }

 private:
  sk_sp<SkImage> sk_image_;
};

class SkiaSVGImage : public Image {
 public:
  SkiaSVGImage(sk_sp<SkSVGDOM> svg_dom)
      : svg_dom_(svg_dom), intrinsic_size_{0.0f, 0.0f} {
    if (svg_dom_ && svg_dom_->getRoot()) {
      SkSize size = svg_dom_->getRoot()->intrinsicSize(
          SkSVGLengthContext(SkSize::Make(0, 0)));
      if (size.width() > 0.0f && size.height() > 0.0f) {
        intrinsic_size_ = {.width = size.width(), .height = size.height()};
      } else if (svg_dom_->getRoot()->getViewBox().has_value()) {
        const SkRect& vb = *svg_dom_->getRoot()->getViewBox();
        if (vb.width() > 0.0f && vb.height() > 0.0f)
          intrinsic_size_ = {.width = vb.width(), .height = vb.height()};
      }
    }
  }
  virtual ~SkiaSVGImage() {}

  SkImage* GetSkImage(const Size& size, bool& matches_dimensions) override {
    matches_dimensions = false;
    return nullptr;
  }

  SkSVGDOM* GetSkSVGDOM(const Size& size) override {
    if (svg_dom_) {
      svg_dom_->setContainerSize(SkSize::Make(size.width, size.height));
    }
    return svg_dom_.get();
  }

  Size GetSize(const Size& container_size) override {
    if (intrinsic_size_.width > 0.0f && intrinsic_size_.height > 0.0f)
      return intrinsic_size_;
    return container_size;
  }

 private:
  sk_sp<SkSVGDOM> svg_dom_;
  Size intrinsic_size_;
};

}  // namespace

Image::~Image() {}

std::shared_ptr<Image> Image::LoadImage(std::string_view path) {
  static std::unordered_map<std::string, std::shared_ptr<Image>> cache;
  std::string path_str(path);
  auto it = cache.find(path_str);
  if (it != cache.end())
    return it->second;

  std::string extension = std::filesystem::path(path).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  sk_sp<SkData> sk_data = LoadFileToSkData(path);
  if (!sk_data) {
    std::cout << "Image::LoadImage: Failed to open/read file: " << path
              << std::endl;
    return nullptr;
  }

  std::shared_ptr<Image> loaded_image;
  if (extension == ".svg") {
    SkMemoryStream stream(sk_data);
    auto svg_dom = SkSVGDOM::MakeFromStream(stream);
    if (svg_dom) {
      loaded_image = std::static_pointer_cast<Image>(
          std::make_shared<SkiaSVGImage>(svg_dom));
    }
  } else {
    auto sk_image = SkImages::DeferredFromEncodedData(sk_data, std::nullopt);
    if (sk_image) {
      loaded_image = std::static_pointer_cast<Image>(
          std::make_shared<RasterImage>(sk_image));
    }
  }
  if (loaded_image)
    cache.emplace( std::move(path_str), loaded_image);
  return loaded_image;
}

std::shared_ptr<Image> Image::FromSkImage(sk_sp<SkImage> sk_image) {
  if (!sk_image) return nullptr;
  return std::static_pointer_cast<Image>(
      std::make_shared<RasterImage>(std::move(sk_image)));
}

}  // namespace ui
}  // namespace perception
