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

#include "panels/tracks_panel.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "undo_manager.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "instruments.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/color_picker_dialog.h"
#include "perception/ui/components/combo_box.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/image_button.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/slider.h"
#include "perception/ui/components/tooltip.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/image.h"
#include "perception/ui/layout.h"
#include "perception/ui/point.h"

using ::perception::ui::DrawContext;

using ::perception::ui::Image;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::ComboBox;
using ::perception::ui::components::Container;
using ::perception::ui::components::ImageButton;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::ShowColorPickerDialog;
using ::perception::ui::components::Slider;
using ::perception::ui::components::Tooltip;
using ::perception::ui::components::UiWindow;

namespace panels {

TracksPanel::TracksPanel(TrackManager& track_manager,
                         std::function<void()> on_track_changed)
    : track_manager_(track_manager),
      on_track_changed_(std::move(on_track_changed)) {
  BuildUI();
}

void TracksPanel::BuildUI() {
  auto scroll_container = ScrollContainer::VerticalScrollContainer(
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetAlignSelf(YGAlignStretch);
            layout.SetWidthPercent(100.0f);
            layout.SetGap(4.0f);
          },
          &tracks_list_node_),
      [](Layout& layout) {
        layout.SetAlignSelf(YGAlignStretch);
        layout.SetWidthPercent(100.0f);
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetMinHeight(0.0f);
      });

  if (tracks_list_node_) {
    tracks_list_node_->OnMouseButtonUp(
        [this](const Point&, perception::window::MouseButton button) {
          if (button == perception::window::MouseButton::Left && is_dragging_) {
            int src_id = dragged_track_id_;
            int target_slot = drop_target_index_;
            is_dragging_ = false;
            dragged_track_id_ = 0;
            drop_target_index_ = -1;
            if (tracks_list_node_) tracks_list_node_->Invalidate();

            if (target_slot >= 0 && src_id != 0) {
              int src_idx = GetTrackIndexById(src_id);
              if (src_idx >= 0 && target_slot != src_idx &&
                  target_slot != src_idx + 1) {
                int target_index =
                    (target_slot < src_idx) ? target_slot : (target_slot - 1);
                std::vector<int> old_order;
                for (const auto& trk : track_manager_.GetTracks()) {
                  old_order.push_back(trk.id);
                }
                if (track_manager_.MoveTrackToIndex(src_id, target_index)) {
                  if (undo_manager_) {
                    undo_manager_->PushAction(
                        std::make_unique<ReorderTracksUndoAction>(old_order));
                  }
                }
              }
            }
            UpdateTrackListUI();
            if (on_track_changed_) on_track_changed_();
          }
        });

    tracks_list_node_->OnMouseHover([this](const Point& pt) {
      if (is_dragging_) {
        const auto& children = tracks_list_node_->GetChildren();
        if (!children.empty()) {
          std::vector<std::shared_ptr<Node>> child_vec(children.begin(),
                                                       children.end());
          size_t num_track_cards =
              child_vec.size() > 0 ? child_vec.size() - 1 : 0;
          if (num_track_cards > 0) {
            int target = 0;
            float pt_y = pt.y;

            std::vector<float> midpoints(num_track_cards);
            for (size_t i = 0; i < num_track_cards; ++i) {
              auto area = child_vec[i]->GetAreaRelativeToParent();
              midpoints[i] = area.origin.y + (area.size.height / 2.0f);
            }

            if (pt_y < midpoints[0]) {
              target = 0;
            } else if (pt_y >= midpoints[num_track_cards - 1]) {
              target = static_cast<int>(num_track_cards);
            } else {
              for (size_t i = 0; i < num_track_cards - 1; ++i) {
                if (pt_y >= midpoints[i] && pt_y < midpoints[i + 1]) {
                  target = static_cast<int>(i + 1);
                  break;
                }
              }
            }

            if (drop_target_index_ != target) {
              drop_target_index_ = target;
              tracks_list_node_->Invalidate();
            }
          }
        }
      }
    });

    tracks_list_node_->OnDrawPostChildren([this](const DrawContext& context) {
      if (!is_dragging_ || drop_target_index_ < 0 || !context.skia_canvas)
        return;

      int src_idx = GetTrackIndexById(dragged_track_id_);
      if (src_idx >= 0) {
        if (drop_target_index_ == src_idx ||
            drop_target_index_ == src_idx + 1) {
          return;
        }
      }

      const auto& children = tracks_list_node_->GetChildren();
      if (children.empty()) return;

      std::vector<std::shared_ptr<Node>> child_vec(children.begin(),
                                                   children.end());
      size_t num_track_cards = child_vec.size() > 0 ? child_vec.size() - 1 : 0;
      if (num_track_cards == 0) return;

      float line_y = 0.0f;
      if (drop_target_index_ < static_cast<int>(num_track_cards)) {
        line_y =
            context.area.origin.y +
            child_vec[drop_target_index_]->GetAreaRelativeToParent().MinY();
      } else {
        line_y =
            context.area.origin.y +
            child_vec[num_track_cards - 1]->GetAreaRelativeToParent().MaxY();
      }

      SkPaint paint;
      paint.setColor(0xFF2ECC71);
      paint.setAntiAlias(true);
      context.skia_canvas->drawRect(
          SkRect::MakeXYWH(context.area.origin.x + 4.0f, line_y - 1.5f,
                           context.area.Width() - 8.0f, 3.0f),
          paint);
    });
  }

  panel_node_ = Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignSelf(YGAlignStretch);
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetPadding(YGEdgeAll, 4.0f);
      },
      scroll_container);

  UpdateTrackListUI();
}

void TracksPanel::UpdateTrackListUI() {
  if (!tracks_list_node_) return;
  tracks_list_node_->RemoveChildren();

  static auto eye_image = Image::LoadImage("/Applications/MusicBox/eye.svg");
  static auto trash_image =
      Image::LoadImage("/Applications/MusicBox/trash.svg");
  static auto bulldozer_image =
      Image::LoadImage("/Applications/MusicBox/bulldozer.svg");

  auto instrument_names = GetInstrumentNames();
  const auto& tracks = track_manager_.GetTracks();
  size_t num_tracks = tracks.size();

  for (size_t i = 0; i < num_tracks; ++i) {
    const auto& track = tracks[i];
    int trk_id = track.id;
    bool is_active = (track_manager_.GetActiveTrackId() == trk_id);
    bool has_notes = !track.notes.empty();
    std::string track_name = track.name;
    int trk_index = static_cast<int>(i);

    uint32 idle_color = is_active ? 0x1A000000 : 0x00000000;
    uint32 hover_color = is_active ? 0x28000000 : 0x0C000000;

    // Card Container for each track item
    auto track_card = Container::VerticalContainer(
        [this, trk_id, trk_index, num_tracks, idle_color,
         hover_color](Node& node) {
          node.SetCursor(perception::window::Cursor::Pointer);
          auto block = node.GetOrAdd<Block>();
          block->SetFillColor(idle_color);
          block->SetBorderRadius(6.0f);

          std::weak_ptr<Node> weak_node = node.shared_from_this();

          node.OnMouseButtonDown(
              [this, trk_id, trk_index](
                  const Point& pt, perception::window::MouseButton button) {
                if (button == perception::window::MouseButton::Left) {
                  is_dragging_ = true;
                  dragged_track_id_ = trk_id;
                  drop_target_index_ = trk_index;

                  if (track_manager_.GetActiveTrackId() != trk_id) {
                    track_manager_.SetActiveTrackId(trk_id);
                    if (on_track_changed_) on_track_changed_();
                  }
                }
              });

          node.OnMouseHover([this, weak_node, trk_index, num_tracks,
                             hover_color](const Point& pt) {
            if (auto n = weak_node.lock()) {
              if (auto block = n->Get<Block>()) {
                block->SetFillColor(hover_color);
              }
              if (is_dragging_ && tracks_list_node_) {
                float container_y =
                    n->GetAreaRelativeToParent().origin.y + pt.y;
                const auto& children = tracks_list_node_->GetChildren();
                if (!children.empty()) {
                  std::vector<std::shared_ptr<Node>> child_vec(children.begin(),
                                                               children.end());
                  size_t num_track_cards =
                      child_vec.size() > 0 ? child_vec.size() - 1 : 0;
                  if (num_track_cards > 0) {
                    int target = 0;
                    std::vector<float> midpoints(num_track_cards);
                    for (size_t i = 0; i < num_track_cards; ++i) {
                      auto area = child_vec[i]->GetAreaRelativeToParent();
                      midpoints[i] = area.origin.y + (area.size.height / 2.0f);
                    }

                    if (container_y < midpoints[0]) {
                      target = 0;
                    } else if (container_y >= midpoints[num_track_cards - 1]) {
                      target = static_cast<int>(num_track_cards);
                    } else {
                      for (size_t i = 0; i < num_track_cards - 1; ++i) {
                        if (container_y >= midpoints[i] &&
                            container_y < midpoints[i + 1]) {
                          target = static_cast<int>(i + 1);
                          break;
                        }
                      }
                    }

                    if (drop_target_index_ != target) {
                      drop_target_index_ = target;
                      tracks_list_node_->Invalidate();
                    }
                  }
                }
              }
            }
          });

          node.OnMouseLeave([weak_node, idle_color]() {
            if (auto n = weak_node.lock()) {
              if (auto block = n->Get<Block>()) {
                block->SetFillColor(idle_color);
              }
            }
          });

          node.OnMouseButtonUp([this](const Point& pt,
                                      perception::window::MouseButton button) {
            if (button == perception::window::MouseButton::Left &&
                is_dragging_) {
              int src_id = dragged_track_id_;
              int target_slot = drop_target_index_;
              is_dragging_ = false;
              dragged_track_id_ = 0;
              drop_target_index_ = -1;
              if (tracks_list_node_) tracks_list_node_->Invalidate();

              if (target_slot >= 0 && src_id != 0) {
                int src_idx = GetTrackIndexById(src_id);
                if (src_idx >= 0 && target_slot != src_idx &&
                    target_slot != src_idx + 1) {
                  int target_index =
                      (target_slot < src_idx) ? target_slot : (target_slot - 1);
                  std::vector<int> old_order;
                  for (const auto& trk : track_manager_.GetTracks()) {
                    old_order.push_back(trk.id);
                  }
                  if (track_manager_.MoveTrackToIndex(src_id, target_index)) {
                    if (undo_manager_) {
                      undo_manager_->PushAction(
                          std::make_unique<ReorderTracksUndoAction>(old_order));
                    }
                  }
                }
              }
              UpdateTrackListUI();
              if (on_track_changed_) on_track_changed_();
            }
          });
        },
        [](Layout& layout) {
          layout.SetAlignSelf(YGAlignStretch);
          layout.SetWidthPercent(100.0f);
          layout.SetPadding(YGEdgeAll, 6.0f);
          layout.SetGap(4.0f);
        },

        // Row 1: Color, Track Name, Mute (M), Solo (S), Eye, Delete
        Container::HorizontalContainer(
            [](Layout& layout) {
              layout.SetAlignSelf(YGAlignStretch);
              layout.SetAlignItems(YGAlignCenter);
              layout.SetJustifyContent(YGJustifySpaceBetween);
            },
            Block::SolidColor(
                track.color,
                [this, trk_id, track](Node& node) {
                  node.SetCursor(perception::window::Cursor::Pointer);
                  node.OnMouseButtonUp(
                      [this, trk_id, track](
                          const Point&,
                          perception::window::MouseButton button) {
                        if (button == perception::window::MouseButton::Left) {
                          ShowColorPickerDialog(
                              "Track color", track.color,
                              [this, trk_id](bool succeeded, uint32 color) {
                                if (succeeded) {
                                  Track* t = track_manager_.GetTrack(trk_id);
                                  if (t) {
                                    uint32 old_color = t->color;
                                    if (old_color == color) return;
                                    t->color = color;
                                    if (undo_manager_) {
                                      undo_manager_->PushAction(
                                          std::make_unique<
                                              ChangeTrackColorUndoAction>(
                                              trk_id, old_color, color));
                                    }
                                    UpdateTrackListUI();
                                    if (on_track_changed_) on_track_changed_();
                                  }
                                }
                              });
                        }
                      });
                },
                [](Block& block) { block.SetBorderRadius(4.0f); },
                Tooltip::ShowTooltip("Change track color"),
                [](Layout& layout) {
                  layout.SetWidth(16.0f);
                  layout.SetHeight(16.0f);
                }),
            InputBox::BasicInputBox(
                track.name, [](Layout& layout) { layout.SetWidth(75.0f); },
                [this, trk_id](InputBox& input_box) {
                  input_box.OnTextChanged(
                      [this, trk_id](std::string_view text) {
                        Track* t = track_manager_.GetTrack(trk_id);
                        if (t) {
                          if (t->name == text) return;
                          std::string old_name = t->name;
                          t->name = std::string(text);
                          if (undo_manager_) {
                            undo_manager_->PushAction(
                                std::make_unique<RenameTrackUndoAction>(
                                    trk_id, old_name, t->name));
                          }
                          if (on_track_changed_) on_track_changed_();
                        }
                      });
                }),
            Button::TextButton(
                "M",
                [this, trk_id]() {
                  Track* t = track_manager_.GetTrack(trk_id);
                  if (t) {
                    t->muted = !t->muted;
                    UpdateTrackListUI();
                    if (on_track_changed_) on_track_changed_();
                  }
                },
                [muted = track.muted](Button& btn) { btn.SetToggled(muted); },
                Tooltip::ShowTooltip("Mute track"),
                [](Layout& layout) { layout.SetWidth(22.0f); }),
            Button::TextButton(
                "S",
                [this, trk_id]() {
                  Track* t = track_manager_.GetTrack(trk_id);
                  if (t) {
                    t->soloed = !t->soloed;
                    UpdateTrackListUI();
                    if (on_track_changed_) on_track_changed_();
                  }
                },
                [soloed = track.soloed](Button& btn) {
                  btn.SetToggled(soloed);
                },
                Tooltip::ShowTooltip("Solo track"),
                [](Layout& layout) { layout.SetWidth(22.0f); }),
            ImageButton::BasicImageButton(
                [this, trk_id]() {
                  Track* t = track_manager_.GetTrack(trk_id);
                  if (t) t->hidden = !t->hidden;
                  UpdateTrackListUI();
                  if (on_track_changed_) on_track_changed_();
                },
                eye_image,
                [visible = !track.hidden](Button& btn) {
                  btn.SetToggled(visible);
                },
                Tooltip::ShowTooltip("Toggle track visibility"),
                [](Layout& layout) {
                  layout.SetWidth(20.0f);
                  layout.SetHeight(20.0f);
                }),
            [&]() -> std::shared_ptr<Node> {
              auto on_delete_click = [this, trk_id, track_name, has_notes]() {
                auto perform_delete = [this, trk_id]() {
                  Track* t = track_manager_.GetTrack(trk_id);
                  int idx = GetTrackIndexById(trk_id);
                  if (t && idx >= 0) {
                    Track deleted = *t;
                    track_manager_.DeleteTrack(trk_id);
                    if (undo_manager_) {
                      undo_manager_->PushAction(
                          std::make_unique<DeleteTrackUndoAction>(deleted, idx));
                    }
                  }
                  UpdateTrackListUI();
                  if (on_track_changed_) on_track_changed_();
                };

                if (!has_notes) {
                  perform_delete();
                  return;
                }

                struct DeleteDialogState {
                  std::weak_ptr<Node> window_node;
                };
                auto state = std::make_shared<DeleteDialogState>();
                auto close_dialog = [state]() {
                  if (auto win = state->window_node.lock()) {
                    if (auto ui_win = win->Get<UiWindow>()) {
                      ui_win->Close();
                    }
                  }
                };

                auto content = Container::VerticalContainer(
                    [](Layout& layout) { layout.SetWidthPercent(100.0f); },
                    Label::BasicLabel(
                        "Are you sure you want to delete track \"" +
                        track_name + "\"?"),
                    Container::HorizontalContainer(
                        [](Layout& layout) {
                          layout.SetJustifyContent(YGJustifyFlexEnd);
                        },
                        Button::TextButton("Cancel", close_dialog),
                        Button::TextButton(
                            "Delete Track",
                            [perform_delete, close_dialog]() {
                              perform_delete();
                              close_dialog();
                            },
                            [](Button& btn) {
                              btn.SetButtonStyle(Button::ButtonStyle::RED);
                            })));

                UiWindow::DialogWithTitleBar(
                    "Delete Track",
                    [close_dialog](UiWindow& window) {
                      window.OnClose([close_dialog]() { close_dialog(); });
                    },
                    [](Layout& layout) {
                      layout.SetWidth(420.0f);
                      layout.SetHeight(140.0f);
                    },
                    content, &state->window_node);
              };

              if (trash_image) {
                return ImageButton::BasicImageButton(
                    on_delete_click, trash_image,
                    Tooltip::ShowTooltip("Delete track"), [](Layout& layout) {
                      layout.SetWidth(20.0f);
                      layout.SetHeight(20.0f);
                    });
              } else {
                return Button::TextButton(
                    "X", on_delete_click, Tooltip::ShowTooltip("Delete track"),
                    [](Layout& layout) { layout.SetWidth(20.0f); });
              }
            }()),

        // Row 2: Instrument ComboBox, Volume Slider, Clear notes
        Container::HorizontalContainer(
            [](Layout& layout) {
              layout.SetAlignSelf(YGAlignStretch);
              layout.SetAlignItems(YGAlignCenter);
              layout.SetJustifyContent(YGJustifySpaceBetween);
            },
            [&]() {
              auto inst_options = GetCategorizedInstrumentOptions();
              int selected_inst_idx =
                  GetOptionIndexForInstrument(track.instrument);
              return ComboBox::BasicComboBox(
                  inst_options, selected_inst_idx,
                  [this, trk_id](int selected) {
                    Track* t = track_manager_.GetTrack(trk_id);
                    if (t) {
                      t->instrument = GetInstrumentFromOptionIndex(selected);
                      if (t->instrument) {
                        t->clef = t->instrument->default_clef;
                      }
                      if (on_track_changed_) on_track_changed_();
                    }
                  },
                  [](Layout& layout) { layout.SetWidth(120.0f); });
            }(),
            Slider::BasicSlider(
                0.0f, 1.0f, track.volume,
                [this, trk_id](float val) {
                  Track* t = track_manager_.GetTrack(trk_id);
                  if (t) t->volume = val;
                },
                Tooltip::ShowTooltip("Volume"),
                [](Layout& layout) { layout.SetWidth(75.0f); }),
            [&]() -> std::shared_ptr<Node> {
              auto on_clear_click = [this, trk_id, track_name, has_notes]() {
                if (!has_notes) return;

                struct ClearDialogState {
                  std::weak_ptr<Node> window_node;
                };
                auto state = std::make_shared<ClearDialogState>();
                auto close_dialog = [state]() {
                  if (auto win = state->window_node.lock()) {
                    if (auto ui_win = win->Get<UiWindow>()) {
                      ui_win->Close();
                    }
                  }
                };
                UiWindow::DialogWithTitleBar(
                    "Clear Track",
                    [close_dialog](UiWindow& window) {
                      window.OnClose([close_dialog]() { close_dialog(); });
                    },
                    [](Layout& layout) { layout.SetWidth(420.0f); },
                    Container::VerticalContainer(
                        [](Layout& layout) { layout.SetWidthPercent(100.0f); },
                        Label::BasicLabel(
                            "Are you sure you want to clear all notes from \"" +
                            track_name + "\"?"),
                        Container::HorizontalContainer(
                            [](Layout& layout) {
                              layout.SetJustifyContent(YGJustifyFlexEnd);
                            },
                            Button::TextButton("Cancel", close_dialog),
                            Button::TextButton("Clear Notes",
                                               [this, trk_id, close_dialog]() {
                                                 track_manager_.ClearTrackNotes(
                                                     trk_id);
                                                 UpdateTrackListUI();
                                                 if (on_track_changed_)
                                                   on_track_changed_();
                                                 close_dialog();
                                               }))),
                    &state->window_node);
              };

              return ImageButton::BasicImageButton(
                  [on_clear_click, has_notes]() {
                    if (has_notes) on_clear_click();
                  },
                  bulldozer_image,
                  [has_notes](Button& btn) {
                    if (!has_notes)
                      btn.SetButtonStyle(Button::ButtonStyle::DISABLED);
                  },
                  Tooltip::ShowTooltip("Clear track notes"),
                  [](Layout& layout) {
                    layout.SetWidth(20.0f);
                    layout.SetHeight(20.0f);
                  });
            }()));

    tracks_list_node_->AddChild(track_card);
  }

  // "+ Add Track" button at the bottom of the track list
  auto add_track_button_row = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetAlignSelf(YGAlignStretch);
        layout.SetMargin(YGEdgeTop, 4.0f);
      },
      Button::TextButton(
          "+ Add Track",
          [this]() {
            int track_num = track_manager_.GetTracks().size() + 1;
            Track* new_track = track_manager_.AddTrack(
                "Track " + std::to_string(track_num), GetDefaultInstrument(),
                GetTrackPresetColor(track_num));
            if (new_track && undo_manager_) {
              undo_manager_->PushAction(
                  std::make_unique<CreateTrackUndoAction>(new_track->id));
            }
            UpdateTrackListUI();
            if (on_track_changed_) on_track_changed_();
          },
          [](Layout& layout) { layout.SetWidth(100.0f); }));

  tracks_list_node_->AddChild(add_track_button_row);
}

int TracksPanel::GetTrackIndexById(int track_id) const {
  const auto& tracks = track_manager_.GetTracks();
  for (size_t i = 0; i < tracks.size(); ++i) {
    if (tracks[i].id == track_id) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace panels
