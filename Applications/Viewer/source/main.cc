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

#include <iostream>
#include <sstream>
#include <string_view>
#include <vector>

#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/time.h"
#include "perception/ui/builders/image_view.h"
#include "perception/ui/builders/node.h"
#include "perception/ui/builders/window.h"
#include "perception/ui/image.h"
#include "perception/ui/node.h"
#include "perception/ui/resize_method.h"
#include "perception/ui/text_alignment.h"

using ::perception::HandOverControl;
using ::perception::SleepForDuration;
using ::perception::TerminateProcess;
using ::perception::ui::builders::AlignContent;
using ::perception::ui::builders::Image;
using ::perception::ui::builders::ImageAlignment;
using ::perception::ui::builders::ImageResizeMethod;
using ::perception::ui::builders::ImageView;
using ::perception::ui::builders::JustifyContent;
using ::perception::ui::builders::OnWindowClose;
using ::perception::ui::builders::Window;
using ::perception::ui::builders::WindowTitle;
using ::perception::ui::Node;
using ::perception::ui::ResizeMethod;
using ::perception::ui::TextAlignment;

namespace {

int opened_instances = 0;
std::vector<std::shared_ptr<Node>> open_windows;

void OpenImage(std::string_view path) {
  std::shared_ptr<::perception::ui::Image> image =
      ::perception::ui::Image::LoadImage(path);
  if (!image) {
    std::cout << "Couldn't load " << path << std::endl;
    return;
  }

  opened_instances++;

  auto window =
      Window(WindowTitle(path), JustifyContent(YGJustifyCenter),
             AlignContent(YGAlignCenter), OnWindowClose([]() {
               opened_instances--;
               if (opened_instances == 0) TerminateProcess();
             }),
             ImageView(ImageAlignment(TextAlignment::MiddleCenter),
                       ImageResizeMethod(ResizeMethod::Contain), Image(image)));
  open_windows.push_back(window);
}

}  // namespace

int main(int argc, char *argv[]) {
  SleepForDuration(std::chrono::seconds(2));

  OpenImage("/Optical 1/Sample Images/1546182636.svg");
  OpenImage("/Optical 1/Sample Images/1530779823.svg");
  OpenImage("/Optical 1/Sample Images/luca-bravo-O453M2Liufs-unsplash.jpg");
  OpenImage(
      "/Optical 1/Sample Images/stephen-leonardi-GUfLILZ-ufI-unsplash.jpg");

  HandOverControl();
  return 0;
}
