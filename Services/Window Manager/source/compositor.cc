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
#include "highlighter.h"
#include "mouse.h"
#include "perception/devices/graphics_device.h"
#include "perception/draw.h"
#include "perception/object_pool.h"
#include "perception/ui/point.h"
#include "perception/ui/rectangle.h"
#include "screen.h"
#include "types.h"
#include "window.h"

namespace graphics = ::perception::devices::graphics;
using ::perception::DrawSprite1bitAlpha;
using ::perception::FillRectangle;
using ::perception::devices::GraphicsDevice;
using ::perception::ui::Point;
using ::perception::ui::Rectangle;

namespace {

bool has_invalidated_area;
Rectangle invalidated_area;

CompositorQuadTree quad_tree;

int z_index;

void AddOpaqueRectangle(
    const std::function<void(QuadRectangle&)>& populate_rectangle) {
  QuadRectangle* rectangle = quad_tree.AllocateRectangle();
  populate_rectangle(*rectangle);
  rectangle->stage = QuadRectangleStage::OPAQUE_TO_SCREEN;
  quad_tree.AddOccludingRectangle(rectangle);
}

void AddAlphaBlendedRectangle(
    const std::function<void(QuadRectangle&)>& populate_rectangle) {
  QuadRectangle* rectangle = quad_tree.AllocateRectangle();
  populate_rectangle(*rectangle);
  rectangle->stage = QuadRectangleStage::ALPHA_TO_WINDOW_MANAGER;
  rectangle->z_index = z_index++;
  quad_tree.DrawAreaToWindowManagerTexture(rectangle->bounds);
  quad_tree.AddRectangle(rectangle);
}

void PopulateCommandsForRectangle(QuadRectangle& rectangle,
                                  std::vector<graphics::Command>& commands,
                                  size_t& last_texture, bool alpha_blend) {
  if (rectangle.IsSolidColor()) {
    // Draw a solid color.
    auto& command = commands.emplace_back();
    command.type = graphics::Command::Type::FILL_RECTANGLE;

    command.fill_rectangle_parameters =
        std::make_shared<graphics::FillRectangleParameters>();
    command.fill_rectangle_parameters->destination = {
        static_cast<uint32>(rectangle.bounds.MinX()),
        static_cast<uint32>(rectangle.bounds.MinY())};
    command.fill_rectangle_parameters->size = {
        static_cast<uint32>(rectangle.bounds.Width()),
        static_cast<uint32>(rectangle.bounds.Height())};
    command.fill_rectangle_parameters->color = rectangle.color;
  } else {
    // Copy the texture.
    if (rectangle.texture_id != last_texture) {
      // Swap over to this texture being the source texture.
      auto& command = commands.emplace_back();
      command.type = graphics::Command::Type::SET_SOURCE_TEXTURE;
      command.texture_reference =
          std::make_shared<graphics::TextureReference>(rectangle.texture_id);
      last_texture = rectangle.texture_id;
    }

    // Copy over this part of the texture.
    {
      auto& command = commands.emplace_back();
      command.type = alpha_blend
                         ? graphics::Command::Type::
                               COPY_PART_OF_A_TEXTURE_WITH_ALPHA_BLENDING
                         : graphics::Command::Type::COPY_PART_OF_A_TEXTURE;
      command.copy_part_of_texture_parameters =
          std::make_shared<graphics::CopyPartOfTextureParameters>();
      command.copy_part_of_texture_parameters->source = {
          static_cast<uint32>(rectangle.texture_offset.x),
          static_cast<uint32>(rectangle.texture_offset.y)};
      command.copy_part_of_texture_parameters->destination = {
          static_cast<uint32>(rectangle.bounds.MinX()),
          static_cast<uint32>(rectangle.bounds.MinY())};
      command.copy_part_of_texture_parameters->size = {
          static_cast<uint32>(rectangle.bounds.Width()),
          static_cast<uint32>(rectangle.bounds.Height())};
    }
  }
}

}  // namespace

void DrawBackground(const Rectangle& screen_area) {
  DrawOpaqueColor(screen_area, kBackgroundColor);
}

void InitializeCompositor() {
  has_invalidated_area = false;
  z_index = 0;
}

void InvalidateScreen(const Rectangle& screen_area) {
  if (has_invalidated_area) {
    invalidated_area = invalidated_area.Union(screen_area);
  } else {
    invalidated_area = screen_area;
    has_invalidated_area = true;
  }
}

void DrawScreen() {
  if (!has_invalidated_area) return;

  SleepUntilWeAreReadyToStartDrawing();

  has_invalidated_area = false;

  Rectangle screen_rectangle{.origin = {0, 0}, .size = GetScreenSize()};
  Rectangle draw_area = invalidated_area.Intersection(screen_rectangle);

  if (draw_area.Width() <= 0 || draw_area.Height() <= 0) return;

  DrawBackground(draw_area);

  (void)Window::ForEachBackToFrontWindow([&](Window& window) {
    window.Draw(draw_area);
    return false;
  });
  // Prep the overlays for drawing, which will mark which areas need to be
  // drawn to the window manager's texture and not directly to the screen.
  DrawHighlighter(draw_area);
  DrawMouse(draw_area);

  // There are 3 stages of commands to construct:
  // (1) Draw any rectangles into the WM Texture.
  //     First opaque rectangle, then z-index sorted alpha blended rectangles.
  // (3) Draw WM textures into framebuffer.
  // (4) Draw rectangles directly into framebuffer.

  std::vector<graphics::Command> draw_into_wm_texture_commands;
  std::vector<graphics::Command> draw_wm_into_framebuffer_commands;
  std::vector<graphics::Command> draw_into_framebuffer_commands;

  std::vector<QuadRectangle*> alpha_blended_quads;

  size_t texture_drawing_into_window_manager = 0;
  size_t texture_drawing_into_framebuffer = 0;

  quad_tree.ForEachItem([&](QuadRectangle* rectangle) {
    switch (rectangle->stage) {
      case QuadRectangleStage::OPAQUE_TO_SCREEN:
        // Draw directly onto the screen.
        PopulateCommandsForRectangle(*rectangle, draw_into_framebuffer_commands,
                                     texture_drawing_into_framebuffer,
                                     /*alpha_blend=*/false);
        break;
      case QuadRectangleStage::OPAQUE_TO_WINDOW_MANAGER: {
        // Draw into the window manager.
        PopulateCommandsForRectangle(*rectangle, draw_into_wm_texture_commands,
                                     texture_drawing_into_window_manager,
                                     /*alpha_blend=*/false);

        // Copy this area into the frame buffer.
        auto& command = draw_wm_into_framebuffer_commands.emplace_back();
        command.type = graphics::Command::Type::COPY_PART_OF_A_TEXTURE;
        command.copy_part_of_texture_parameters =
            std::make_shared<graphics::CopyPartOfTextureParameters>();
        command.copy_part_of_texture_parameters->source = {
            static_cast<uint32>(rectangle->bounds.MinX()),
            static_cast<uint32>(rectangle->bounds.MinY())};
        command.copy_part_of_texture_parameters->destination = {
            static_cast<uint32>(rectangle->bounds.MinX()),
            static_cast<uint32>(rectangle->bounds.MinY())};
        command.copy_part_of_texture_parameters->size = {
            static_cast<uint32>(rectangle->bounds.Width()),
            static_cast<uint32>(rectangle->bounds.Height())};
        break;
      }
      case QuadRectangleStage::ALPHA_TO_WINDOW_MANAGER:
        // Sort by z-index and draw later.
        alpha_blended_quads.push_back(rectangle);
        return;
    }
  });

  std::sort(alpha_blended_quads.begin(), alpha_blended_quads.end(),
            [](const QuadRectangle* a, const QuadRectangle* b) {
              return a->z_index < b->z_index;
            });

  for (auto rectangle : alpha_blended_quads) {
    // Because the backmost content is always opaque (if there are no windows
    // open then it is the background color), the command has already been
    // populated to copy the window manager's texture into the command buffer.
    PopulateCommandsForRectangle(*rectangle, draw_into_wm_texture_commands,
                                 texture_drawing_into_window_manager,
                                 /*alpha_blend=*/true);
  }

  // Merge all the draw commands together.
  graphics::Commands commands;
  commands.commands.reserve(draw_into_wm_texture_commands.size() +
                            draw_wm_into_framebuffer_commands.size() +
                            draw_into_framebuffer_commands.size());
  if (!draw_into_wm_texture_commands.empty()) {
    // There are things to draw into the window manager's texture.

    // Set destination to be the wm texture.
    auto& command = commands.commands.emplace_back();
    command.type = graphics::Command::Type::SET_DESTINATION_TEXTURE;
    command.texture_reference = std::make_shared<graphics::TextureReference>(
        GetWindowManagerTextureId());

    for (auto& c : draw_into_wm_texture_commands)
      commands.commands.push_back(std::move(c));
  }

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

  z_index = 0;

  // Reset the quad tree.
  quad_tree.Reset();
}

void DrawOpaqueColor(const Rectangle& screen_area, uint32 fill_color) {
  AddOpaqueRectangle([screen_area, fill_color](QuadRectangle& rectangle) {
    rectangle.bounds = screen_area;
    rectangle.texture_id = 0;
    rectangle.color = fill_color;
  });
}

void DrawAlphaBlendedColor(const Rectangle& screen_area, uint32 fill_color) {
  AddAlphaBlendedRectangle([screen_area, fill_color](QuadRectangle& rectangle) {
    rectangle.bounds = screen_area;
    rectangle.texture_id = 0;
    rectangle.color = fill_color;
  });
}

void CopyOpaqueTexture(const Rectangle& screen_area, size_t texture_id,
                       const Point& offset) {
  AddOpaqueRectangle(
      [screen_area, texture_id, offset](QuadRectangle& rectangle) {
        rectangle.bounds = screen_area;
        rectangle.texture_id = texture_id;
        rectangle.texture_offset = offset;
      });
}

void CopyAlphaBlendedTexture(const Rectangle& screen_area, size_t texture_id,
                             const Point& offset) {
  AddAlphaBlendedRectangle(
      [screen_area, texture_id, offset](QuadRectangle& rectangle) {
        rectangle.bounds = screen_area;
        rectangle.texture_id = texture_id;
        rectangle.texture_offset = offset;
      });
}