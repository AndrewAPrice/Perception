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

namespace perception {
namespace ui {
namespace {

class RasterImage : public Image {
 public:
  RasterImage(sk_sp<SkImage> image) : sk_image_(image) {}
  virtual ~RasterImage() {}

  SkImage* GetSkImage(float width, float height,
                      bool& matches_dimensions) override {
    matches_dimensions = std::abs(width - sk_image_->width()) < 1.0f &&
                         std::abs(height - sk_image_->height()) < 1.0f;
    return sk_image_.get();
  }

  void GetSize(float container_width, float container_height, float& width,
               float& height) override {
    width = sk_image_->width();
    height = sk_image_->height();
  }

 private:
  sk_sp<SkImage> sk_image_;
};

class SVGImage : public Image {
 public:
  SVGImage(sk_sp<SkSVGDOM> svg_dom)
      : sk_svg_dom_(svg_dom), last_width_(-1), last_height_(-1) {}
  virtual ~SVGImage() {}

  virtual SkSVGDOM* GetSkSVGDOM(float width, float height) {
    if (std::abs(width - last_width_) >= 1.0f ||
        std::abs(height - last_height_) >= 1.0f) {
      sk_svg_dom_->setContainerSize(SkSize::Make(width, height));
      last_width_ = width;
      last_height_ = height;
    }

    return sk_svg_dom_.get();
  }

  void GetSize(float container_width, float container_height, float& width,
               float& height) override {
    auto size = sk_svg_dom_->getRoot()->intrinsicSize(
        SkSVGLengthContext(SkSize::Make(container_width, container_height)));
    width = (int)(size.width());
    height = (int)(size.height());
  }

 private:
  sk_sp<SkSVGDOM> sk_svg_dom_;

  int last_width_, last_height_;
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
            SkString(std::filesystem::path(path).parent_path().c_str()),
            /*predecode=*/true),
        /*predecode=*/true);

    auto sk_svg_dom =
        SkSVGDOM::Builder().setResourceProvider(std::move(rp)).make(*sk_stream);

    if (sk_svg_dom) return std::make_shared<SVGImage>(sk_svg_dom);
  } else {
    auto sk_data = SkData::MakeFromFileName(std::string(path).c_str());
    if (!sk_data) return nullptr;

    auto sk_image = SkImage::MakeFromEncoded(sk_data);
    if (sk_image) return std::make_shared<RasterImage>(sk_image);
  }

  return nullptr;
}

}  // namespace ui
}  // namespace perception