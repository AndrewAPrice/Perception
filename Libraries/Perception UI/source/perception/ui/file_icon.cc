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

#include "perception/ui/file_icon.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/label.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/text_alignment.h"

using ::perception::ui::GetBold12UiFont;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Label;

namespace perception {
namespace ui {
namespace {

// Color of folder back tab for regular directories.
constexpr uint32 kFolderTabColor = SkColorSetARGB(0xFF, 0xB4, 0x53, 0x09);

// Color of paper sheet inside folder.
constexpr uint32 kFolderPaperColor = SkColorSetARGB(0xFF, 0xFE, 0xF3, 0xC7);

// Color of folder front flap for regular directories.
constexpr uint32 kFolderFlapColor = SkColorSetARGB(0xFF, 0xF5, 0x9E, 0x0B);

// Highlight color for top lip of folder flap.
constexpr uint32 kFolderHighlightColor = SkColorSetARGB(0xFF, 0xFB, 0xBF, 0x24);

// Primary white color for paper document sheets.
constexpr uint32 kDocumentPaperColor = SkColorSetARGB(0xFF, 0xFF, 0xFF, 0xFF);

// Border color for document paper fold and outline.
constexpr uint32 kDocumentBorderColor = SkColorSetARGB(0xFF, 0xD1, 0xD5, 0xDB);

// Folded corner color for paper document sheet.
constexpr uint32 kDocumentFoldColor = SkColorSetARGB(0xFF, 0xE5, 0xE7, 0xEB);

// Accent color for text document lines.
constexpr uint32 kTextLineColor = SkColorSetARGB(0xFF, 0x06, 0xB6, 0xD4);

// Accent color for Markdown logo (Indigo/Blue).
constexpr uint32 kMarkdownLogoColor = SkColorSetARGB(0xFF, 0x43, 0x38, 0xCA);

// Accent color for song musical note (Rose/Pink).
constexpr uint32 kSongNoteColor = SkColorSetARGB(0xFF, 0xDB, 0x27, 0x77);

// Shell body color for audio cassette tape.
constexpr uint32 kCassetteBodyColor = SkColorSetARGB(0xFF, 0x37, 0x41, 0x51);

// Label window color for audio cassette tape.
constexpr uint32 kCassetteWindowColor = SkColorSetARGB(0xFF, 0xF3, 0xF4, 0xF6);

// Spool reel color for audio cassette tape.
constexpr uint32 kCassetteSpoolColor = SkColorSetARGB(0xFF, 0xF9, 0x73, 0x16);

// Border frame color for photo image icon.
constexpr uint32 kPhotoImageBorderColor = SkColorSetARGB(0xFF, 0xE5, 0xE7, 0xEB);

// Sky color for photo image icon.
constexpr uint32 kPhotoImageSkyColor = SkColorSetARGB(0xFF, 0x0E, 0x74, 0x90);

// Sun color for photo image icon.
constexpr uint32 kPhotoImageSunColor = SkColorSetARGB(0xFF, 0xFB, 0xBF, 0x24);

// Mountain color for photo image icon.
constexpr uint32 kPhotoImageMountainColor = SkColorSetARGB(0xFF, 0x10, 0xB9, 0x81);

// Application window frame header background color.
constexpr uint32 kAppWindowHeaderColor = SkColorSetARGB(0xFF, 0x7C, 0x3A, 0xED);

// Application window body background color.
constexpr uint32 kAppWindowBodyColor = SkColorSetARGB(0xFF, 0xF3, 0xE8, 0xFF);

// Application grid tile color.
constexpr uint32 kAppTileColor = SkColorSetARGB(0xFF, 0x8B, 0x5C, 0xF6);

// Library book 1 spine color (Teal).
constexpr uint32 kBook1Color = SkColorSetARGB(0xFF, 0x0D, 0x94, 0x88);

// Library book 2 spine color (Amber).
constexpr uint32 kBook2Color = SkColorSetARGB(0xFF, 0xD9, 0x77, 0x06);

// Library book 3 spine color (Indigo).
constexpr uint32 kBook3Color = SkColorSetARGB(0xFF, 0x4F, 0x46, 0xE5);

std::string GetExtension(std::string_view name) {
  std::string ext = std::filesystem::path(name).extension().string();
  if (!ext.empty() && ext[0] == '.') {
    ext.erase(0, 1);
  }
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext;
}

// Draws base paper sheet with folded top-right corner.
void DrawDocumentBase(SkCanvas* canvas, float x, float y) {
  SkPaint paint;
  paint.setAntiAlias(true);

  // Main page path (with top-right corner cut for fold)
  SkPathBuilder page_builder;
  page_builder.moveTo(x + 4.0f, y + 2.0f);
  page_builder.lineTo(x + 16.0f, y + 2.0f);
  page_builder.lineTo(x + 20.0f, y + 6.0f);
  page_builder.lineTo(x + 20.0f, y + 22.0f);
  page_builder.lineTo(x + 4.0f, y + 22.0f);
  page_builder.close();
  SkPath page_path = page_builder.detach();

  // Draw white page body
  paint.setColor(kDocumentPaperColor);
  paint.setStyle(SkPaint::kFill_Style);
  canvas->drawPath(page_path, paint);

  // Page outline border
  paint.setColor(kDocumentBorderColor);
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(1.0f);
  canvas->drawPath(page_path, paint);

  // Folded top-right corner flap
  SkPathBuilder fold_builder;
  fold_builder.moveTo(x + 16.0f, y + 2.0f);
  fold_builder.lineTo(x + 16.0f, y + 6.0f);
  fold_builder.lineTo(x + 20.0f, y + 6.0f);
  fold_builder.close();
  SkPath fold_path = fold_builder.detach();

  paint.setColor(kDocumentFoldColor);
  paint.setStyle(SkPaint::kFill_Style);
  canvas->drawPath(fold_path, paint);

  paint.setColor(kDocumentBorderColor);
  paint.setStyle(SkPaint::kStroke_Style);
  canvas->drawPath(fold_path, paint);
}

// Draws photo frame with mountains and sun for image files.
void DrawImageIcon(SkCanvas* canvas, float x, float y) {
  SkPaint paint;
  paint.setAntiAlias(true);

  // Outer border / photo frame
  paint.setColor(kPhotoImageBorderColor);
  paint.setStyle(SkPaint::kFill_Style);
  canvas->drawRoundRect({x + 2.0f, y + 3.0f, x + 22.0f, y + 21.0f}, 2.0f, 2.0f, paint);

  // Sky area
  paint.setColor(kPhotoImageSkyColor);
  canvas->drawRoundRect({x + 3.0f, y + 4.0f, x + 21.0f, y + 20.0f}, 1.5f, 1.5f, paint);

  // Sun
  paint.setColor(kPhotoImageSunColor);
  canvas->drawCircle(x + 16.5f, y + 7.5f, 2.0f, paint);

  // Overlapping mountain peaks
  SkPathBuilder m1_builder;
  m1_builder.moveTo(x + 3.0f, y + 20.0f);
  m1_builder.lineTo(x + 9.5f, y + 12.0f);
  m1_builder.lineTo(x + 16.0f, y + 20.0f);
  m1_builder.close();
  paint.setColor(SkColorSetARGB(0xCC, 0x05, 0x96, 0x69));
  canvas->drawPath(m1_builder.detach(), paint);

  SkPathBuilder m2_builder;
  m2_builder.moveTo(x + 9.0f, y + 20.0f);
  m2_builder.lineTo(x + 15.0f, y + 10.0f);
  m2_builder.lineTo(x + 21.0f, y + 20.0f);
  m2_builder.close();
  paint.setColor(kPhotoImageMountainColor);
  canvas->drawPath(m2_builder.detach(), paint);
}

// Draws audio cassette tape for audio (.wav) files.
void DrawAudioIcon(SkCanvas* canvas, float x, float y) {
  SkPaint paint;
  paint.setAntiAlias(true);

  // Cassette tape body
  paint.setColor(kCassetteBodyColor);
  paint.setStyle(SkPaint::kFill_Style);
  canvas->drawRoundRect({x + 2.5f, y + 6.0f, x + 21.5f, y + 18.0f}, 1.5f, 1.5f, paint);

  // Cassette outer shell thin outline border
  paint.setColor(SkColorSetARGB(0xFF, 0x11, 0x18, 0x27));
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(0.8f);
  canvas->drawRoundRect({x + 2.5f, y + 6.0f, x + 21.5f, y + 18.0f}, 1.5f, 1.5f, paint);

  // Label window
  paint.setColor(kCassetteWindowColor);
  paint.setStyle(SkPaint::kFill_Style);
  canvas->drawRoundRect({x + 4.5f, y + 8.5f, x + 19.5f, y + 15.5f}, 0.8f, 0.8f, paint);

  // Left tape spool
  paint.setColor(kCassetteSpoolColor);
  canvas->drawCircle(x + 8.5f, y + 12.0f, 1.9f, paint);
  paint.setColor(kCassetteBodyColor);
  canvas->drawCircle(x + 8.5f, y + 12.0f, 0.8f, paint);

  // Right tape spool
  paint.setColor(kCassetteSpoolColor);
  canvas->drawCircle(x + 15.5f, y + 12.0f, 1.9f, paint);
  paint.setColor(kCassetteBodyColor);
  canvas->drawCircle(x + 15.5f, y + 12.0f, 0.8f, paint);

  // Connecting tape bridge
  paint.setColor(kCassetteBodyColor);
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(0.8f);
  canvas->drawLine(x + 8.5f, y + 13.8f, x + 15.5f, y + 13.8f, paint);
}

// Draws double musical note on paper sheet for song (.song) files.
void DrawSongIcon(SkCanvas* canvas, float x, float y) {
  DrawDocumentBase(canvas, x, y);

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(kSongNoteColor);
  paint.setStyle(SkPaint::kFill_Style);

  // Noteheads
  canvas->drawCircle(x + 8.0f, y + 17.0f, 2.0f, paint);
  canvas->drawCircle(x + 15.0f, y + 14.5f, 2.0f, paint);

  // Stems
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(1.5f);
  canvas->drawLine(x + 9.5f, y + 17.0f, x + 9.5f, y + 8.5f, paint);
  canvas->drawLine(x + 16.5f, y + 14.5f, x + 16.5f, y + 6.0f, paint);

  // Beam connecting stems
  SkPathBuilder beam_builder;
  beam_builder.moveTo(x + 9.0f, y + 9.5f);
  beam_builder.lineTo(x + 9.0f, y + 7.0f);
  beam_builder.lineTo(x + 17.2f, y + 4.5f);
  beam_builder.lineTo(x + 17.2f, y + 7.0f);
  beam_builder.close();

  paint.setStyle(SkPaint::kFill_Style);
  canvas->drawPath(beam_builder.detach(), paint);
}

// Draws text lines on paper sheet for text (.txt, .json, .xml, .cfg) files.
void DrawTextIcon(SkCanvas* canvas, float x, float y) {
  DrawDocumentBase(canvas, x, y);

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(kTextLineColor);
  paint.setStyle(SkPaint::kFill_Style);

  // Horizontal text lines
  canvas->drawRoundRect({x + 7.0f, y + 9.0f, x + 15.0f, y + 10.5f}, 0.5f, 0.5f, paint);
  canvas->drawRoundRect({x + 7.0f, y + 12.5f, x + 17.0f, y + 14.0f}, 0.5f, 0.5f, paint);
  canvas->drawRoundRect({x + 7.0f, y + 16.0f, x + 14.0f, y + 17.5f}, 0.5f, 0.5f, paint);
}

// Draws Markdown M↓ logo inside paper sheet for markdown (.md) files.
void DrawMarkdownIcon(SkCanvas* canvas, float x, float y) {
  DrawDocumentBase(canvas, x, y);

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(kMarkdownLogoColor);
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(1.4f);
  paint.setStrokeCap(SkPaint::kRound_Cap);
  paint.setStrokeJoin(SkPaint::kRound_Join);

  // 'M' letter path
  SkPathBuilder m_builder;
  m_builder.moveTo(x + 6.5f, y + 16.5f);
  m_builder.lineTo(x + 6.5f, y + 10.5f);
  m_builder.lineTo(x + 9.0f, y + 13.5f);
  m_builder.lineTo(x + 11.5f, y + 10.5f);
  m_builder.lineTo(x + 11.5f, y + 16.5f);
  canvas->drawPath(m_builder.detach(), paint);

  // Downward arrow '↓' stem
  canvas->drawLine(x + 15.0f, y + 10.5f, x + 15.0f, y + 16.5f, paint);

  // Downward arrow '↓' head
  SkPathBuilder arrow_builder;
  arrow_builder.moveTo(x + 13.2f, y + 14.7f);
  arrow_builder.lineTo(x + 15.0f, y + 16.5f);
  arrow_builder.lineTo(x + 16.8f, y + 14.7f);
  canvas->drawPath(arrow_builder.detach(), paint);
}

// Draws application window and launcher grid for app (.app) files.
void DrawAppIcon(SkCanvas* canvas, float x, float y) {
  SkPaint paint;
  paint.setAntiAlias(true);

  // Main window body
  paint.setColor(kAppWindowBodyColor);
  paint.setStyle(SkPaint::kFill_Style);
  canvas->drawRoundRect({x + 2.0f, y + 3.0f, x + 22.0f, y + 21.0f}, 2.5f, 2.5f, paint);

  // Header / Title bar
  paint.setColor(kAppWindowHeaderColor);
  SkPathBuilder header_builder;
  header_builder.moveTo(x + 2.0f, y + 5.5f);
  header_builder.lineTo(x + 2.0f, y + 3.0f);
  header_builder.lineTo(x + 22.0f, y + 3.0f);
  header_builder.lineTo(x + 22.0f, y + 8.0f);
  header_builder.lineTo(x + 2.0f, y + 8.0f);
  header_builder.close();
  canvas->drawPath(header_builder.detach(), paint);

  // Title bar dots
  paint.setColor(SkColorSetARGB(0xFF, 0xEF, 0x44, 0x44));
  canvas->drawCircle(x + 5.0f, y + 5.5f, 1.0f, paint);
  paint.setColor(SkColorSetARGB(0xFF, 0xFA, 0xCC, 0x15));
  canvas->drawCircle(x + 7.5f, y + 5.5f, 1.0f, paint);
  paint.setColor(SkColorSetARGB(0xFF, 0x22, 0xC5, 0x5E));
  canvas->drawCircle(x + 10.0f, y + 5.5f, 1.0f, paint);

  // 4-tile launcher grid inside body
  paint.setColor(kAppTileColor);
  canvas->drawRoundRect({x + 5.0f, y + 10.0f, x + 11.0f, y + 14.0f}, 1.0f, 1.0f, paint);
  canvas->drawRoundRect({x + 13.0f, y + 10.0f, x + 19.0f, y + 14.0f}, 1.0f, 1.0f, paint);
  canvas->drawRoundRect({x + 5.0f, y + 15.0f, x + 11.0f, y + 19.0f}, 1.0f, 1.0f, paint);
  canvas->drawRoundRect({x + 13.0f, y + 15.0f, x + 19.0f, y + 19.0f}, 1.0f, 1.0f, paint);
}

// Draws 3 standing library books for library (.lib) files.
void DrawLibraryIcon(SkCanvas* canvas, float x, float y) {
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setStyle(SkPaint::kFill_Style);

  // Shelf line at bottom
  paint.setColor(SkColorSetARGB(0xFF, 0x9C, 0xA3, 0xAF));
  canvas->drawRoundRect({x + 2.0f, y + 20.0f, x + 22.0f, y + 22.0f}, 0.5f, 0.5f, paint);

  // Book 1 (Teal)
  paint.setColor(kBook1Color);
  canvas->drawRoundRect({x + 4.0f, y + 6.0f, x + 8.5f, y + 20.0f}, 1.0f, 1.0f, paint);

  // Book 2 (Amber)
  paint.setColor(kBook2Color);
  canvas->drawRoundRect({x + 9.5f, y + 4.0f, x + 14.0f, y + 20.0f}, 1.0f, 1.0f, paint);

  // Book 3 (Indigo)
  paint.setColor(kBook3Color);
  canvas->drawRoundRect({x + 15.0f, y + 7.0f, x + 19.5f, y + 20.0f}, 1.0f, 1.0f, paint);

  // Gold spine accent lines on books
  paint.setColor(SkColorSetARGB(0xFF, 0xFE, 0xF0, 0x8A));
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(1.0f);
  canvas->drawLine(x + 5.0f, y + 9.0f, x + 7.5f, y + 9.0f, paint);
  canvas->drawLine(x + 10.5f, y + 7.0f, x + 13.0f, y + 7.0f, paint);
  canvas->drawLine(x + 16.0f, y + 10.0f, x + 18.5f, y + 10.0f, paint);
}

// Draws generic file icon for unknown extensions.
void DrawOtherIcon(SkCanvas* canvas, float x, float y) {
  DrawDocumentBase(canvas, x, y);
}

}  // namespace

std::shared_ptr<Node> CreateFileIcon(bool is_directory, bool is_symlink,
                                     std::string_view name) {
  std::shared_ptr<Node> container = Node::Empty([](Layout& layout) {
    layout.SetWidth(24.0f);
    layout.SetHeight(24.0f);
    layout.SetPositionType(YGPositionTypeRelative);
  });

  if (is_directory) {
    container->OnDraw([](const DrawContext& context) {
      SkCanvas* canvas = context.skia_canvas;
      float x = context.area.origin.x;
      float y = context.area.origin.y;

      SkPaint paint;
      paint.setAntiAlias(true);

      // Folder back tab
      paint.setColor(kFolderTabColor);
      canvas->drawRoundRect({x + 2.0f, y + 2.0f, x + 12.0f, y + 8.0f}, 2.0f,
                            2.0f, paint);

      // Paper sheet sticking out
      paint.setColor(kFolderPaperColor);
      canvas->drawRoundRect({x + 4.0f, y + 5.0f, x + 20.0f, y + 12.0f}, 1.0f,
                            1.0f, paint);

      // Folder front flap
      paint.setColor(kFolderFlapColor);
      canvas->drawRoundRect({x + 1.0f, y + 7.0f, x + 23.0f, y + 22.0f}, 3.0f,
                            3.0f, paint);

      // Top lip highlight
      paint.setColor(kFolderHighlightColor);
      canvas->drawRoundRect({x + 2.0f, y + 7.0f, x + 22.0f, y + 9.0f}, 1.0f,
                            1.0f, paint);
    });
  } else {
    std::string ext = GetExtension(name);
    void (*draw_fn)(SkCanvas*, float, float);

    if (ext == "jpg" || ext == "png" || ext == "svg" || ext == "rgba" ||
        ext == "jpeg" || ext == "ttf" || ext == "bmp") {
      draw_fn = DrawImageIcon;
    } else if (ext == "wav") {
      draw_fn = DrawAudioIcon;
    } else if (ext == "song") {
      draw_fn = DrawSongIcon;
    } else if (ext == "txt" || ext == "json" || ext == "xml" || ext == "cfg" ||
               ext == "conf") {
      draw_fn = DrawTextIcon;
    } else if (ext == "md") {
      draw_fn = DrawMarkdownIcon;
    } else if (ext == "app") {
      draw_fn = DrawAppIcon;
    } else if (ext == "lib" || ext == "so") {
      draw_fn = DrawLibraryIcon;
    } else {
      draw_fn = DrawOtherIcon;
    }

    container->OnDraw([draw_fn](const DrawContext& context) {
      draw_fn(context.skia_canvas, context.area.origin.x, context.area.origin.y);
    });
  }

  if (is_symlink) {
    container->AddChild(Node::Empty(
        [](Layout& layout) {
          layout.SetPositionType(YGPositionTypeAbsolute);
          layout.SetPosition(YGEdgeBottom, -2.0f);
          layout.SetPosition(YGEdgeRight, -2.0f);
          layout.SetWidth(12.0f);
          layout.SetHeight(12.0f);
        },
        [](Block& block) {
          block.SetFillColor(0xFFFFFFFF);
          block.SetBorderRadius(4.0f);
          block.SetBorderColor(0xFF1F2937);
          block.SetBorderWidth(1.0f);
        },
        Label::BasicLabel(
            "↗",
            [](Layout& layout) {
              layout.SetWidth(10.0f);
              layout.SetHeight(10.0f);
            },
            [](Label& label) {
              label.SetTextAlignment(TextAlignment::MiddleCenter);
              label.SetColor(0xFF1F2937);
              label.SetFont(GetBold12UiFont());
            })));
  }

  return container;
}

}  // namespace ui
}  // namespace perception

