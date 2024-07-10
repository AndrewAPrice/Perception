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
#include <iostream>

#include "include/core/SkData.h"
#include "include/core/SkStream.h"
#include "modules/svg/include/SkSVGRenderContext.h"
#include "modules/svg/include/SkSVGSVG.h"
#include "perception/ui/size.h"

namespace perception {
namespace ui {
namespace {

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

class SVGImage : public Image {
 public:
  SVGImage(sk_sp<SkSVGDOM> svg_dom)
      : sk_svg_dom_(svg_dom), last_size_{.width = -1, .height = -1} {}
  virtual ~SVGImage() {}

  SkImage* GetSkImage(const Size& size, bool& matches_dimensions) override {
    return nullptr;
  }

  virtual SkSVGDOM* GetSkSVGDOM(const Size& size) override {
    if (size != last_size_) {
      sk_svg_dom_->setContainerSize(SkSize::Make(size.width, size.height));
      last_size_ = size;
    }

    return sk_svg_dom_.get();
  }

  Size GetSize(const Size& container_size) override {
    auto size = sk_svg_dom_->getRoot()->intrinsicSize(SkSVGLengthContext(
        SkSize::Make(container_size.width, container_size.height)));
    return {.width = size.width(), .height = size.height()};
  }

 private:
  sk_sp<SkSVGDOM> sk_svg_dom_;

  Size last_size_;
};

}  // namespace

Image::~Image() {}

std::shared_ptr<Image> Image::LoadImage(std::string_view path) {
  std::string extension = std::filesystem::path(path).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (extension == ".svg") {
    auto sk_stream = SkStream::MakeFromFile(std::string(path).c_str());
    auto rp = skresources::DataURIResourceProviderProxy::Make(
        skresources::FileResourceProvider::Make(
            SkString(std::filesystem::path(path).parent_path().c_str())));

    auto sk_svg_dom =
        SkSVGDOM::Builder().setResourceProvider(std::move(rp)).make(*sk_stream);

    if (sk_svg_dom) {
      return std::static_pointer_cast<Image>(
          std::make_shared<SVGImage>(sk_svg_dom));
    }
  } else {
    auto sk_data = SkData::MakeFromFileName(std::string(path).c_str());
    if (!sk_data) return nullptr;

    auto sk_image = SkImages::DeferredFromEncodedData(sk_data, std::nullopt);
    if (sk_image)
      return std::static_pointer_cast<Image>(
          std::make_shared<RasterImage>(sk_image));
  }

  return nullptr;
}

}  // namespace ui
}  // namespace perception
