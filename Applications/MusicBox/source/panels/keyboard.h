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

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include "include/core/SkCanvas.h"
#include "perception/ui/keyboard.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/ui/size.h"
#include "track_manager.h"

namespace panels {

class Keyboard {
 public:
  explicit Keyboard(TrackManager& track_manager);
  ~Keyboard() = default;

  std::shared_ptr<perception::ui::Node> GetNode() const { return node_; }
  void Invalidate();

  // Callback setters
  void OnKeyHover(
      std::function<void(int key_index, const perception::ui::Point& pt)>
          callback);
  void OnKeyDown(std::function<void(int key_index)> callback);
  void OnKeyUp(std::function<void(int key_index)> callback);

  // Key state tracking
  void SetKeyPressed(int key_index, bool is_pressed);
  bool IsKeyPressed(int key_index) const;
  const std::set<int>& GetPressedKeys() const { return pressed_keys_; }

  void SetOctaves(int bottom_octave, int top_octave);
  int GetBottomOctave() const { return bottom_octave_; }
  int GetTopOctave() const { return top_octave_; }
  void OnOctavesChanged(
      std::function<void(int bottom_octave, int top_octave)> callback);

  // Key positioning and math utilities
  static int GetKeyAtPosition(const perception::ui::Point& point,
                              const perception::ui::Size& size);
  static bool IsKeyBlack(int key_index);
  static int GetWhiteKeyCount(int key_index);

  std::optional<int> MapKeyCodeToKeyIndex(
      perception::ui::KeyCode key_code) const;

 private:
  void BuildNode();
  void Draw(SkCanvas& canvas, const perception::ui::Size& size);
  void DrawKeys(SkCanvas& canvas, const perception::ui::Size& size,
                bool is_black, float white_key_w, float key_w, float key_h,
                const std::map<int, uint32>& active_highlights,
                const SkFont& font);
  void DrawSingleKey(SkCanvas& canvas, int key_index, bool is_black,
                     float x_left, float key_w, float key_h, float total_h,
                     const std::map<int, uint32>& active_highlights,
                     const SkFont& font);
  void DrawOctaveBands(SkCanvas& canvas, const perception::ui::Size& size,
                       const SkFont& font);

  TrackManager& track_manager_;
  std::shared_ptr<perception::ui::Node> node_;
  std::set<int> pressed_keys_;

  int bottom_octave_ = 3;
  int top_octave_ = 4;
  bool dragging_bottom_octave_ = false;
  bool dragging_top_octave_ = false;

  std::function<void(int key_index, const perception::ui::Point& pt)>
      on_key_hover_;
  std::function<void(int key_index)> on_key_down_;
  std::function<void(int key_index)> on_key_up_;
  std::function<void(int bottom_octave, int top_octave)> on_octaves_changed_;
};

}  // namespace panels
