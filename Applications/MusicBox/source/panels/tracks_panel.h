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
#include <memory>
#include <string>
#include <vector>

#include "perception/ui/node.h"
#include "track_manager.h"

class UndoManager;

namespace panels {

class TracksPanel {
 public:
  TracksPanel(TrackManager& track_manager,
              std::function<void()> on_track_changed);

  void SetUndoManager(UndoManager* undo_manager) {
    undo_manager_ = undo_manager;
  }

  std::shared_ptr<perception::ui::Node> GetNode() const { return panel_node_; }

  void UpdateTrackListUI();

 private:
  void BuildUI();
  int GetTrackIndexById(int track_id) const;

  TrackManager& track_manager_;
  UndoManager* undo_manager_ = nullptr;
  std::function<void()> on_track_changed_;

  std::shared_ptr<perception::ui::Node> panel_node_;
  std::shared_ptr<perception::ui::Node> tracks_list_node_;

  // Drag and drop reordering state
  bool is_dragging_ = false;
  int dragged_track_id_ = 0;
  int drop_target_index_ = -1;
};

}  // namespace panels
