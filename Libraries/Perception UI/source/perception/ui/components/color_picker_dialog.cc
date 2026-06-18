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

#include "perception/ui/components/color_picker_dialog.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "include/core/SkColor.h"
#include "perception/scheduler.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/group_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/slider.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/theme.h"
#include "perception/window/mouse_button.h"

using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::GroupBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::Slider;
using ::perception::ui::components::UiWindow;
using ButtonStyle = ::perception::ui::components::Button::ButtonStyle;

namespace perception {
namespace ui {
namespace components {
namespace {

struct HSV {
  float h;  // 0..360
  float s;  // 0..100
  float v;  // 0..100

  float& operator[](int index) { return index == 0 ? h : (index == 1 ? s : v); }

  float operator[](int index) const {
    return index == 0 ? h : (index == 1 ? s : v);
  }
};

HSV RGBToHSV(uint32 rgb) {
  uint8 r = (rgb >> 16) & 0xFF;
  uint8 g = (rgb >> 8) & 0xFF;
  uint8 b = rgb & 0xFF;

  float rf = r / 255.0f;
  float gf = g / 255.0f;
  float bf = b / 255.0f;

  float max_val = std::max({rf, gf, bf});
  float min_val = std::min({rf, gf, bf});
  float delta = max_val - min_val;

  float h = 0.0f;
  if (delta > 0.0f) {
    if (max_val == rf) {
      h = 60.0f * (std::fmod(((gf - bf) / delta), 6.0f));
    } else if (max_val == gf) {
      h = 60.0f * (((bf - rf) / delta) + 2.0f);
    } else if (max_val == bf) {
      h = 60.0f * (((rf - gf) / delta) + 4.0f);
    }
  }
  if (h < 0.0f) h += 360.0f;

  float s = (max_val == 0.0f) ? 0.0f : (delta / max_val) * 100.0f;
  float v = max_val * 100.0f;

  return {h, s, v};
}

uint32 HSVToRGB(HSV hsv) {
  float h = hsv.h;
  float s = hsv.s / 100.0f;
  float v = hsv.v / 100.0f;

  float c = v * s;
  float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;

  float r = 0.0f, g = 0.0f, b = 0.0f;
  if (h >= 0.0f && h < 60.0f) {
    r = c;
    g = x;
    b = 0.0f;
  } else if (h >= 60.0f && h < 120.0f) {
    r = x;
    g = c;
    b = 0.0f;
  } else if (h >= 120.0f && h < 180.0f) {
    r = 0.0f;
    g = c;
    b = x;
  } else if (h >= 180.0f && h < 240.0f) {
    r = 0.0f;
    g = x;
    b = c;
  } else if (h >= 240.0f && h < 300.0f) {
    r = x;
    g = 0.0f;
    b = c;
  } else if (h >= 300.0f && h <= 360.0f) {
    r = c;
    g = 0.0f;
    b = x;
  }

  uint8 ru = static_cast<uint8>(std::round((r + m) * 255.0f));
  uint8 gu = static_cast<uint8>(std::round((g + m) * 255.0f));
  uint8 bu = static_cast<uint8>(std::round((b + m) * 255.0f));

  return (0xFF << 24) | (ru << 16) | (gu << 8) | bu;
}

struct ColorPickerDialogState {
  uint32 initial_color;
  uint32 current_color;

  std::shared_ptr<Node> new_preview_block;

  // RGB UI elements
  std::shared_ptr<Node> rgb_sliders[3];
  std::shared_ptr<Node> rgb_labels[3];

  // HSV UI elements
  std::shared_ptr<Node> hsv_sliders[3];
  std::shared_ptr<Node> hsv_labels[3];

  std::shared_ptr<Node> save_button;

  std::function<void(bool succeeded, uint32 color)> on_selected;
  std::shared_ptr<bool> completed;
  std::weak_ptr<Node> window_node;
};

std::vector<std::shared_ptr<Node>> active_dialogs;

void CloseDialog(std::shared_ptr<Node> window_node) {
  ::perception::Defer([window_node]() {
    auto it =
        std::find(active_dialogs.begin(), active_dialogs.end(), window_node);
    if (it != active_dialogs.end()) {
      active_dialogs.erase(it);
    }
  });
}

void HandleCallback(const std::shared_ptr<ColorPickerDialogState>& state,
                    bool success, uint32 color) {
  if (*state->completed) return;
  *state->completed = true;

  state->on_selected(success, color | 0xFF000000);

  if (!state->window_node.expired()) {
    CloseDialog(state->window_node.lock());
  }
}

void UpdateColorPickerUI(const std::shared_ptr<ColorPickerDialogState>& state,
                         uint32 new_color, bool update_rgb, bool update_hsv) {
  state->current_color = new_color | 0xFF000000;

  uint8 rgb[3] = {static_cast<uint8>((state->current_color >> 16) & 0xFF),
                  static_cast<uint8>((state->current_color >> 8) & 0xFF),
                  static_cast<uint8>(state->current_color & 0xFF)};

  // Update new color preview block
  if (state->new_preview_block) {
    auto block = state->new_preview_block->Get<Block>();
    if (block) {
      block->SetFillColor(state->current_color);
      state->new_preview_block->Invalidate();
    }
  }

  // Update RGB text labels
  for (int i = 0; i < 3; ++i) {
    if (state->rgb_labels[i]) {
      state->rgb_labels[i]->Get<Label>()->SetText(std::to_string(rgb[i]));
    }
  }

  // Update RGB sliders
  if (update_rgb) {
    for (int i = 0; i < 3; ++i) {
      if (state->rgb_sliders[i]) {
        state->rgb_sliders[i]->Get<Slider>()->SetValue(rgb[i]);
      }
    }
  }

  HSV hsv = RGBToHSV(state->current_color);
  const char* hsv_units[3] = {"°", "%", "%"};

  // Update HSV text labels
  for (int i = 0; i < 3; ++i) {
    if (state->hsv_labels[i]) {
      state->hsv_labels[i]->Get<Label>()->SetText(
          std::to_string(static_cast<int>(std::round(hsv[i]))) + hsv_units[i]);
    }
  }

  // Update HSV sliders
  if (update_hsv) {
    for (int i = 0; i < 3; ++i) {
      if (state->hsv_sliders[i]) {
        state->hsv_sliders[i]->Get<Slider>()->SetValue(hsv[i]);
      }
    }
  }

  // Update Save button active state
  if (state->save_button) {
    bool changed =
        (state->current_color & 0xFFFFFF) != (state->initial_color & 0xFFFFFF);
    state->save_button->Get<Button>()->SetButtonStyle(
        changed ? ButtonStyle::PRIMARY : ButtonStyle::DISABLED);
  }
}

}  // namespace

void ShowColorPickerDialog(
    std::string_view title, uint32 initial_color,
    std::function<void(bool succeeded, uint32 color)> on_selected) {
  auto state = std::make_shared<ColorPickerDialogState>();
  state->initial_color = initial_color | 0xFF000000;
  state->current_color = initial_color | 0xFF000000;
  state->on_selected = on_selected;
  state->completed = std::make_shared<bool>(false);

  uint8 rgb_init[3] = {static_cast<uint8>((state->initial_color >> 16) & 0xFF),
                       static_cast<uint8>((state->initial_color >> 8) & 0xFF),
                       static_cast<uint8>(state->initial_color & 0xFF)};

  HSV hsv_init = RGBToHSV(state->initial_color);

  auto create_slider_row =
      [&](std::string_view name, float min, float max, float initial_val,
          std::shared_ptr<Node>& slider_out, std::shared_ptr<Node>& label_out,
          std::function<void(float)> on_change) {
        return Container::HorizontalContainer(
            [](Layout& layout) {
              layout.SetAlignItems(YGAlignCenter);
              layout.SetGap(kColorPickerRowGap);
              layout.SetWidthPercent(100.0f);
            },
            Label::BasicLabel(
                name,
                [](Layout& layout) { layout.SetWidth(kColorPickerLabelWidth); },
                [](Label& label) {
                  label.SetColor(kColorPickerLabelColor);
                  label.SetTextAlignment(TextAlignment::MiddleLeft);
                }),
            Slider::BasicSlider(
                min, max, initial_val, on_change,
                [](Layout& layout) { layout.SetFlexGrow(1.0f); }, &slider_out),
            Label::BasicLabel(
                "",
                [](Layout& layout) {
                  layout.SetWidth(kColorPickerValueLabelWidth);
                },
                [](Label& label) {
                  label.SetColor(kColorPickerLabelColor);
                  label.SetTextAlignment(TextAlignment::MiddleRight);
                },
                &label_out));
      };

  const char* rgb_names[3] = {"R:", "G:", "B:"};
  std::shared_ptr<Node> rgb_rows[3];
  for (int i = 0; i < 3; ++i) {
    rgb_rows[i] = create_slider_row(
        rgb_names[i], 0.0f, 255.0f, rgb_init[i], state->rgb_sliders[i],
        state->rgb_labels[i], [state, i](float val) {
          uint8 rgb[3] = {
              static_cast<uint8>((state->current_color >> 16) & 0xFF),
              static_cast<uint8>((state->current_color >> 8) & 0xFF),
              static_cast<uint8>(state->current_color & 0xFF)};
          rgb[i] = static_cast<uint8>(val);
          UpdateColorPickerUI(
              state, (0xFF << 24) | (rgb[0] << 16) | (rgb[1] << 8) | rgb[2],
              /*update_rgb=*/false, /*update_hsv=*/true);
        });
  }

  const char* hsv_names[3] = {"H:", "S:", "V:"};
  const float hsv_max[3] = {360.0f, 100.0f, 100.0f};
  std::shared_ptr<Node> hsv_rows[3];
  for (int i = 0; i < 3; ++i) {
    hsv_rows[i] = create_slider_row(
        hsv_names[i], 0.0f, hsv_max[i], hsv_init[i], state->hsv_sliders[i],
        state->hsv_labels[i], [state, i](float val) {
          HSV hsv = RGBToHSV(state->current_color);
          hsv[i] = val;
          UpdateColorPickerUI(state, HSVToRGB(hsv), /*update_rgb=*/true,
                              /*update_hsv=*/false);
        });
  }

  auto window_node = UiWindow::DialogWithTitleBar(
      title,
      [state](UiWindow& window) {
        window.OnClose([state]() {
          ::perception::Defer([state]() {
            HandleCallback(state, false, state->initial_color);
          });
        });
      },
      [](Layout& layout) {
        layout.SetWidth(kColorPickerDialogWidth);
        layout.SetHeight(kColorPickerDialogHeight);
      },
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
            layout.SetPadding(YGEdgeAll, kColorPickerDialogPadding);
            layout.SetGap(kColorPickerDialogGap);
          },
          [](Block& block) {
            block.SetFillColor(kColorPickerDialogBackgroundColor);
          },  // Side-by-side color preview blocks
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetWidthPercent(100.0f);
                layout.SetGap(kColorPickerPreviewGap);
              },
              // Old color block.
              Node::Empty(
                  [](Layout& layout) {
                    layout.SetFlexGrow(1.0f);
                    layout.SetHeight(kColorPickerPreviewHeight);
                  },
                  [state](Block& block) {
                    block.SetFillColor(state->initial_color);
                    block.SetBorderColor(kColorPickerPreviewBorderColor);
                    block.SetBorderWidth(kColorPickerPreviewBorderWidth);
                    block.SetBorderRadius(kColorPickerPreviewBorderRadius);
                  }),
              // New color block.
              Node::Empty(
                  [](Layout& layout) {
                    layout.SetFlexGrow(1.0f);
                    layout.SetHeight(kColorPickerPreviewHeight);
                  },
                  [state](Block& block) {
                    block.SetFillColor(state->current_color);
                    block.SetBorderColor(kColorPickerPreviewBorderColor);
                    block.SetBorderWidth(kColorPickerPreviewBorderWidth);
                    block.SetBorderRadius(kColorPickerPreviewBorderRadius);
                  },
                  &state->new_preview_block)),
          GroupBox::VerticalGroupBox("RGB Color", rgb_rows[0], rgb_rows[1],
                                     rgb_rows[2]),
          GroupBox::VerticalGroupBox("HSV Color", hsv_rows[0], hsv_rows[1],
                                     hsv_rows[2]),
          // Footer buttons.
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetWidthPercent(100.0f);
                layout.SetJustifyContent(YGJustifyFlexEnd);
                layout.SetGap(kWidgetSpacing);
              },
              Button::TextButton(
                  "Cancel",
                  [state]() {
                    ::perception::Defer([state]() {
                      HandleCallback(state, false, state->initial_color);
                    });
                  },
                  [](Layout& layout) {
                    layout.SetWidth(kColorPickerButtonWidth);
                    layout.SetHeight(kColorPickerButtonHeight);
                  },
                  [](Button& button) {
                    button.SetButtonStyle(ButtonStyle::LIGHT);
                  }),
              Button::TextButton(
                  "Save",
                  [state]() {
                    ::perception::Defer([state]() {
                      HandleCallback(state, true, state->current_color);
                    });
                  },
                  [](Layout& layout) {
                    layout.SetWidth(kColorPickerButtonWidth);
                    layout.SetHeight(kColorPickerButtonHeight);
                  },
                  [](Button& button) {
                    button.SetButtonStyle(ButtonStyle::DISABLED);
                  },
                  &state->save_button))),
      &state->window_node);

  active_dialogs.push_back(window_node);

  // Initialize display labels
  UpdateColorPickerUI(state, state->initial_color, /*update_rgb=*/true,
                      /*update_hsv=*/true);
}

}  // namespace components
}  // namespace ui
}  // namespace perception
