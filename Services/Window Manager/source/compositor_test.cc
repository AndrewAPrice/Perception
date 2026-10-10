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

#include "compositor.h"

#include "perception/devices/graphics_device.h"
#include "perception/serialization/text_serializer.h"
#include "perception/ui/point.h"
#include "perception/ui/rectangle.h"
#include "perception/window/window_manager.h"
#include "screen.h"
#include "testing.h"
#include "window.h"

namespace {

using ::perception::ui::Point;
using ::perception::ui::Rectangle;
using ::perception::window::CreateWindowRequest;
namespace graphics = ::perception::devices::graphics;

TEST(CompositorInitializationAndInvalidation) {
  Window::UnfocusAllWindows();
  InitializeScreen();
  InitializeCompositor();

  // Invalidate a specific screen area
  Rectangle screen_area{.origin = {.x = 100.0f, .y = 100.0f},
                        .size = {.width = 400.0f, .height = 300.0f}};
  InvalidateScreen(screen_area);

  // Trigger draw cycle for invalidated screen areas
  DrawScreen();

  // Second draw should be no-op since dirty regions were cleared
  DrawScreen();
}

TEST(CompositorDrawOpaqueColor) {
  Window::UnfocusAllWindows();
  InitializeScreen();
  InitializeCompositor();

  Rectangle screen_area{.origin = {.x = 50.0f, .y = 50.0f},
                        .size = {.width = 200.0f, .height = 150.0f}};

  DrawOpaqueColor(screen_area, 0xFF00FF00);
  InvalidateScreen(screen_area);
  DrawScreen();

  const auto& last_opaque = GetLastRunDrawCommands();
  EXPECT((size_t)2, last_opaque.commands.size());
  std::vector<std::string> expected_opaque = {
      R"({
  Type: 0
  Texture reference: {
    Id: 0
  }
})",
      R"({
  Type: 8
  Fill rectangle parameters: {
    Destination: {
      Left: 50
      Top: 50
    }
    Size: {
      Width: 200
      Height: 150
    }
    Color: 4283341055
  }
})"};
  for (size_t i = 0;
       i < last_opaque.commands.size() && i < expected_opaque.size(); i++) {
    EXPECT(expected_opaque[i], ::perception::serialization::SerializeToString(
                                   last_opaque.commands[i]));
  }
}

TEST(CompositorCopyOpaqueTexture) {
  Window::UnfocusAllWindows();
  InitializeScreen();
  InitializeCompositor();

  Rectangle screen_area{.origin = {.x = 50.0f, .y = 50.0f},
                        .size = {.width = 200.0f, .height = 150.0f}};
  Point offset{.x = 0.0f, .y = 0.0f};

  CopyOpaqueTexture(screen_area, /*texture_id=*/10, offset);
  InvalidateScreen(screen_area);
  DrawScreen();

  const auto& last_tex = GetLastRunDrawCommands();
  EXPECT((size_t)2, last_tex.commands.size());
  std::vector<std::string> expected_tex = {
      R"({
  Type: 0
  Texture reference: {
    Id: 0
  }
})",
      R"({
  Type: 8
  Fill rectangle parameters: {
    Destination: {
      Left: 50
      Top: 50
    }
    Size: {
      Width: 200
      Height: 150
    }
    Color: 4283341055
  }
})"};
  for (size_t i = 0; i < last_tex.commands.size() && i < expected_tex.size();
       i++) {
    EXPECT(expected_tex[i], ::perception::serialization::SerializeToString(
                                last_tex.commands[i]));
  }
}

TEST(CompositorDrawAlphaBlendedColor) {
  Window::UnfocusAllWindows();
  InitializeScreen();
  InitializeCompositor();

  Rectangle screen_area{.origin = {.x = 50.0f, .y = 50.0f},
                        .size = {.width = 200.0f, .height = 150.0f}};

  DrawAlphaBlendedColor(screen_area, HIGHLIGHTER_TINT);
  InvalidateScreen(screen_area);
  DrawScreen();

  const auto& last_alpha = GetLastRunDrawCommands();
  EXPECT((size_t)2, last_alpha.commands.size());
  std::vector<std::string> expected_alpha = {
      R"({
  Type: 0
  Texture reference: {
    Id: 0
  }
})",
      R"({
  Type: 8
  Fill rectangle parameters: {
    Destination: {
      Left: 50
      Top: 50
    }
    Size: {
      Width: 200
      Height: 150
    }
    Color: 4283341055
  }
})"};
  for (size_t i = 0;
       i < last_alpha.commands.size() && i < expected_alpha.size(); i++) {
    EXPECT(expected_alpha[i], ::perception::serialization::SerializeToString(
                                  last_alpha.commands[i]));
  }
}

TEST(CompositorCopyAlphaBlendedTexture) {
  Window::UnfocusAllWindows();
  InitializeScreen();
  InitializeCompositor();

  Rectangle screen_area{.origin = {.x = 50.0f, .y = 50.0f},
                        .size = {.width = 200.0f, .height = 150.0f}};
  Point offset{.x = 0.0f, .y = 0.0f};

  CopyAlphaBlendedTexture(screen_area, /*texture_id=*/20, offset);
  InvalidateScreen(screen_area);
  DrawScreen();

  const auto& last_alpha_tex = GetLastRunDrawCommands();
  EXPECT((size_t)2, last_alpha_tex.commands.size());
  std::vector<std::string> expected_alpha_tex = {
      R"({
  Type: 0
  Texture reference: {
    Id: 0
  }
})",
      R"({
  Type: 8
  Fill rectangle parameters: {
    Destination: {
      Left: 50
      Top: 50
    }
    Size: {
      Width: 200
      Height: 150
    }
    Color: 4283341055
  }
})"};
  for (size_t i = 0;
       i < last_alpha_tex.commands.size() && i < expected_alpha_tex.size();
       i++) {
    EXPECT(expected_alpha_tex[i],
           ::perception::serialization::SerializeToString(
               last_alpha_tex.commands[i]));
  }
}

TEST(CompositorDisjointInvalidationAreas) {
  Window::UnfocusAllWindows();
  InitializeScreen();
  InitializeCompositor();

  Rectangle area1{.origin = {.x = 50.0f, .y = 50.0f},
                  .size = {.width = 100.0f, .height = 100.0f}};
  Rectangle area2{.origin = {.x = 400.0f, .y = 400.0f},
                  .size = {.width = 50.0f, .height = 50.0f}};

  InvalidateScreen(area1);
  InvalidateScreen(area2);
  DrawScreen();

  const auto& last_cmds = GetLastRunDrawCommands();
  // Should have SetDestination (Screen) + 2 fill rects for area1 and area2 (coalescing does not merge them because they are disjoint)
  EXPECT((size_t)3, last_cmds.commands.size());
  EXPECT(graphics::Command::Type::SET_DESTINATION_TEXTURE,
         last_cmds.commands[0].type);
  EXPECT(graphics::Command::Type::FILL_RECTANGLE, last_cmds.commands[1].type);
  EXPECT(graphics::Command::Type::FILL_RECTANGLE, last_cmds.commands[2].type);

  // Total area filled should be 100*100 + 50*50 = 12500, NOT the union (400*400 = 160000)
  uint32 total_pixels = 0;
  for (size_t i = 1; i < last_cmds.commands.size(); i++) {
    total_pixels += last_cmds.commands[i].fill_rectangle_parameters->size.width *
                    last_cmds.commands[i].fill_rectangle_parameters->size.height;
  }
  EXPECT((uint32)12500, total_pixels);
}

TEST(CompositorCommandCoalescing) {
  Window::UnfocusAllWindows();
  InitializeScreen();
  InitializeCompositor();

  Rectangle combined_area{.origin = {.x = 100.0f, .y = 100.0f},
                          .size = {.width = 200.0f, .height = 50.0f}};
  InvalidateScreen(combined_area);
  DrawScreen();

  const auto& last_cmds = GetLastRunDrawCommands();
  EXPECT((size_t)2, last_cmds.commands.size());
  EXPECT(graphics::Command::Type::SET_DESTINATION_TEXTURE,
         last_cmds.commands[0].type);
  EXPECT(graphics::Command::Type::FILL_RECTANGLE, last_cmds.commands[1].type);
  EXPECT((uint32)200,
         last_cmds.commands[1].fill_rectangle_parameters->size.width);
  EXPECT((uint32)50,
         last_cmds.commands[1].fill_rectangle_parameters->size.height);
}

}  // namespace
