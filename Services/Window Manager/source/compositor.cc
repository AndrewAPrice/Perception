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

#include "compositor.h"

#include "compositor_quad_tree.h"
#include "frame.h"
#include "highlighter.h"
#include "mouse.h"
#include "perception/devices/graphics_device.h"
#include "perception/draw.h"
#include "perception/object_pool.h"
#include "screen.h"
#include "types.h"
#include "window.h"

namespace graphics = ::perception::devices::graphics;
using ::perception::DrawSprite1bitAlpha;
using ::perception::FillRectangle;
using ::perception::devices::GraphicsDevice;

namespace {

bool has_invalidated_area;
int invalidated_area_min_x;
int invalidated_area_min_y;
int invalidated_area_max_x;
int invalidated_area_max_y;

CompositorQuadTree quad_tree;

}  // namespace

void DrawBackground(int min_x, int min_y, int max_x, int max_y) {
  DrawSolidColor(min_x, min_y, max_x, max_y, kBackgroundColor);
}

void InitializeCompositor() { has_invalidated_area = false; }

void InvalidateScreen(int min_x, int min_y, int max_x, int max_y) {
  if (has_invalidated_area) {
    invalidated_area_min_x = std::min(min_x, invalidated_area_min_x);
    invalidated_area_min_y = std::min(min_y, invalidated_area_min_y);
    invalidated_area_max_x = std::max(max_x, invalidated_area_max_x);
    invalidated_area_max_y = std::max(max_y, invalidated_area_max_y);
  } else {
    invalidated_area_min_x = min_x;
    invalidated_area_min_y = min_y;
    invalidated_area_max_x = max_x;
    invalidated_area_max_y = max_y;
    has_invalidated_area = true;
  }
}

void DrawScreen() {
  if (!has_invalidated_area) return;

  SleepUntilWeAreReadyToStartDrawing();

  has_invalidated_area = false;
  int min_x = std::max(0, invalidated_area_min_x);
  int min_y = std::max(0, invalidated_area_min_y);
  int max_x = std::min(GetScreenWidth(), invalidated_area_max_x);
  int max_y = std::min(GetScreenHeight(), invalidated_area_max_y);

  DrawBackground(min_x, min_y, max_x, max_y);

  Frame* root_frame = Frame::GetRootFrame();
  if (root_frame) root_frame->Draw(min_x, min_y, max_x, max_y);

  Window::ForEachBackToFrontDialog(
      [&](Window& window) { window.Draw(min_x, min_y, max_x, max_y); });

  // Prep the overlays for drawing, which will mark which areas need to be
  // drawn to the window manager's texture and not directly to the screen.
  PrepHighlighterForDrawing(min_x, min_y, max_x, max_y);
  PrepMouseForDrawing(min_x, min_y, max_x, max_y);

  // There are 3 stages of commands that we want to construct:
  // (1) Draw any rectangles into the WM Texture.
  // (2) Draw mouse/highlighter into the WM Texture.
  // (3) Draw WM textures into framebuffer.
  // (4) Draw window textures into framebuffer.

  std::vector<graphics::Command> draw_into_wm_texture_commands;
  std::vector<graphics::Command> draw_wm_into_framebuffer_commands;
  std::vector<graphics::Command> draw_into_framebuffer_commands;

  size_t texture_drawing_into_window_manager = 0;
  size_t texture_drawing_into_framebuffer = 0;

  quad_tree.ForEachItem([&](Rectangle* rectangle) {
    if (rectangle->draw_into_wm_texture) {
      // Draw this into the WM texture.
      if (rectangle->texture_id != GetWindowManagerTextureId()) {
        // This is NOT the window manager texture, which is already
        // applied to the window manager texture.

        // Draw this rectangle into the window manager's texture.

        if (rectangle->IsSolidColor()) {
          // Draw solid color into window manager texture.
          auto& command = draw_into_wm_texture_commands.emplace_back();
          command.type = graphics::Command::Type::FILL_RECTANGLE;

          command.fill_rectangle_parameters =
              std::make_shared<graphics::FillRectangleParameters>();
          command.fill_rectangle_parameters->destination = {
              static_cast<uint32>(rectangle->min_x),
              static_cast<uint32>(rectangle->min_y)};
          command.fill_rectangle_parameters->size = {
              static_cast<uint32>(rectangle->max_x - rectangle->min_x),
              static_cast<uint32>(rectangle->max_y - rectangle->min_y)};
          command.fill_rectangle_parameters->color = rectangle->color;
        } else {
          // Copy texture into window manager's texture.
          if (rectangle->texture_id != texture_drawing_into_window_manager) {
            // Switch over to source texture.
            auto& command = draw_into_wm_texture_commands.emplace_back();
            command.type = graphics::Command::Type::SET_SOURCE_TEXTURE;
            command.texture_reference =
                std::make_shared<graphics::TextureReference>(
                    rectangle->texture_id);
            texture_drawing_into_window_manager = rectangle->texture_id;
          }

          {
            auto& command = draw_into_wm_texture_commands.emplace_back();
            command.type = graphics::Command::Type::COPY_PART_OF_A_TEXTURE;
            command.copy_part_of_texture_parameters =
                std::make_shared<graphics::CopyPartOfTextureParameters>();
            command.copy_part_of_texture_parameters->source = {
                static_cast<uint32>(rectangle->texture_x),
                static_cast<uint32>(rectangle->texture_y)};
            command.copy_part_of_texture_parameters->destination = {
                static_cast<uint32>(rectangle->min_x),
                static_cast<uint32>(rectangle->min_y)};
            command.copy_part_of_texture_parameters->size = {
                static_cast<uint32>(rectangle->max_x - rectangle->min_x),
                static_cast<uint32>(rectangle->max_y - rectangle->min_y)};
          }
        }
      }

      // Now copy this area from the Window Manager's texture into the
      // framebuffer.
      {
        auto& command = draw_wm_into_framebuffer_commands.emplace_back();
        command.type = graphics::Command::Type::COPY_PART_OF_A_TEXTURE;
        command.copy_part_of_texture_parameters =
            std::make_shared<graphics::CopyPartOfTextureParameters>();
        command.copy_part_of_texture_parameters->source = {
            static_cast<uint32>(rectangle->min_x),
            static_cast<uint32>(rectangle->min_y)};
        command.copy_part_of_texture_parameters->destination = {
            static_cast<uint32>(rectangle->min_x),
            static_cast<uint32>(rectangle->min_y)};
        command.copy_part_of_texture_parameters->size = {
            static_cast<uint32>(rectangle->max_x - rectangle->min_x),
            static_cast<uint32>(rectangle->max_y - rectangle->min_y)};
      }
    } else {
      // Draw this rectangle straight into the framebuffer.
      if (rectangle->IsSolidColor()) {
        // Draw solid color into framebuffer.
        auto& command = draw_wm_into_framebuffer_commands.emplace_back();
        command.type = graphics::Command::Type::FILL_RECTANGLE;

        command.fill_rectangle_parameters =
            std::make_shared<graphics::FillRectangleParameters>();
        command.fill_rectangle_parameters->destination = {
            static_cast<uint32>(rectangle->min_x),
            static_cast<uint32>(rectangle->min_y)};
        command.fill_rectangle_parameters->size = {
            static_cast<uint32>(rectangle->max_x - rectangle->min_x),
            static_cast<uint32>(rectangle->max_y - rectangle->min_y)};
        command.fill_rectangle_parameters->color = rectangle->color;
      } else {
        // Copy texture into framebuffer.
        if (rectangle->texture_id != texture_drawing_into_framebuffer) {
          // Switch over the source texture.
          auto& command = draw_into_framebuffer_commands.emplace_back();
          command.type = graphics::Command::Type::SET_SOURCE_TEXTURE;
          command.texture_reference =
              std::make_shared<graphics::TextureReference>(
                  rectangle->texture_id);
          texture_drawing_into_framebuffer = rectangle->texture_id;
        }

        {
          auto& command = draw_into_framebuffer_commands.emplace_back();
          command.type = graphics::Command::Type::COPY_PART_OF_A_TEXTURE;
          command.copy_part_of_texture_parameters =
              std::make_shared<graphics::CopyPartOfTextureParameters>();
          command.copy_part_of_texture_parameters->source = {
              static_cast<uint32>(rectangle->texture_x),
              static_cast<uint32>(rectangle->texture_y)};
          command.copy_part_of_texture_parameters->destination = {
              static_cast<uint32>(rectangle->min_x),
              static_cast<uint32>(rectangle->min_y)};
          command.copy_part_of_texture_parameters->size = {
              static_cast<uint32>(rectangle->max_x - rectangle->min_x),
              static_cast<uint32>(rectangle->max_y - rectangle->min_y)};
        }
      }
    }
  });

  // Merge all the draw commands together.
  graphics::Commands commands;
  commands.commands.reserve(draw_into_wm_texture_commands.size() +
                            draw_wm_into_framebuffer_commands.size() +
                            draw_into_framebuffer_commands.size());
  if (!draw_into_wm_texture_commands.empty()) {
    // We have things to draw into the window manager's texture.

    // Set destination to be the wm texture.
    auto& command = commands.commands.emplace_back();
    command.type = graphics::Command::Type::SET_DESTINATION_TEXTURE;
    command.texture_reference = std::make_shared<graphics::TextureReference>(
        GetWindowManagerTextureId());

    for (auto& c : draw_into_wm_texture_commands)
      commands.commands.push_back(std::move(c));
  }

  // Draw some overlays.
  DrawHighlighter(commands, min_x, min_y, max_x, max_y);
  DrawMouse(commands, min_x, min_y, max_x, max_y);

  // Set destination to frame buffer.
  {
    auto& command = commands.commands.emplace_back();
    command.type = graphics::Command::Type::SET_DESTINATION_TEXTURE;
    command.texture_reference =
        std::make_shared<graphics::TextureReference>(0);  // The screen.
  }

  if (!draw_wm_into_framebuffer_commands.empty()) {
    auto& command = commands.commands.emplace_back();
    command.type = graphics::Command::Type::SET_SOURCE_TEXTURE;
    command.texture_reference = std::make_shared<graphics::TextureReference>(
        GetWindowManagerTextureId());

    for (auto& c : draw_wm_into_framebuffer_commands)
      commands.commands.push_back(std::move(c));
  }

  for (auto& c : draw_into_framebuffer_commands)
    commands.commands.push_back(std::move(c));

  RunDrawCommands(std::move(commands));

  // Reset the quad tree.
  quad_tree.Reset();
}

// Draws a solid color on the screen.
void DrawSolidColor(int min_x, int min_y, int max_x, int max_y,
                    uint32 fill_color) {
  Rectangle* rectangle = quad_tree.AllocateRectangle();
  rectangle->min_x = min_x;
  rectangle->min_y = min_y;
  rectangle->max_x = max_x;
  rectangle->max_y = max_y;
  rectangle->texture_id = 0;
  rectangle->color = fill_color;
  rectangle->draw_into_wm_texture = false;
  quad_tree.AddOccludingRectangle(rectangle);
}

// Copies a part of a texture onto the screen.
void CopyTexture(int min_x, int min_y, int max_x, int max_y, size_t texture_id,
                 int texture_x, int texture_y) {
  Rectangle* rectangle = quad_tree.AllocateRectangle();
  rectangle->min_x = min_x;
  rectangle->min_y = min_y;
  rectangle->max_x = max_x;
  rectangle->max_y = max_y;
  rectangle->texture_id = texture_id;
  rectangle->texture_x = texture_x;
  rectangle->texture_y = texture_y;

  if (texture_id == GetWindowManagerTextureId()) {
    // We're copying from the window manager's texture, so we can consider
    // the same as textures that we are going to draw into the window
    // manager.
    rectangle->draw_into_wm_texture = true;
  } else {
    rectangle->draw_into_wm_texture = false;
  }
  quad_tree.AddOccludingRectangle(rectangle);
}

void CopySectionOfScreenIntoWindowManagersTexture(int min_x, int min_y,
                                                  int max_x, int max_y) {
  quad_tree.DrawAreaToWindowManagerTexture(min_x, min_y, max_x, max_y);
}
