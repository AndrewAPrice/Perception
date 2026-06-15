// Copyright 2026
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

#include <cstdlib>
#include <ctime>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include "perception/processes.h"
#include "perception/random.h"
#include "perception/scheduler.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"

using ::perception::HandOverControl;
using ::perception::TerminateProcess;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::Label;
using ::perception::ui::components::UiWindow;

namespace {

constexpr int kRows = 9;
constexpr int kCols = 9;
constexpr int kMines = 10;

enum class GameState { PLAYING, WON, LOST };
GameState game_state = GameState::PLAYING;

struct Cell {
  bool is_mine = false;
  bool is_revealed = false;
  bool is_flagged = false;
  int neighbor_mines = 0;

  std::shared_ptr<Block> block;
  std::shared_ptr<Label> label;
  std::shared_ptr<Node> node;
};

Cell cells[kRows][kCols];
std::shared_ptr<Label> mine_counter_label;
std::shared_ptr<Label> reset_button_label;

std::string_view ResetButtonString() {
  switch (game_state) {
    case GameState::WON:
      return "Win!";
    case GameState::LOST:
      return "Game Over";
    default:
      return "New";
  }
}

void UpdateCellVisuals(int r, int c) {
  Cell& cell = cells[r][c];
  if (!cell.is_revealed) {
    cell.block->SetFillColor(0xFFCCCCCC);
    if (cell.is_flagged) {
      cell.label->SetText("F");
      cell.label->SetColor(0xFFCC0000);
    } else {
      cell.label->SetText("");
    }
  } else {
    if (cell.is_mine) {
      cell.block->SetFillColor(0xFFFF8888);
      cell.label->SetText("*");
      cell.label->SetColor(0xFF000000);
    } else {
      cell.block->SetFillColor(0xFFEEEEEE);
      if (cell.neighbor_mines > 0) {
        std::stringstream ss;
        ss << cell.neighbor_mines;
        cell.label->SetText(ss.str());
        uint32 color = 0xFF000000;
        switch (cell.neighbor_mines) {
          case 1:
            color = 0xFF0000FF;
            break;
          case 2:
            color = 0xFF008800;
            break;
          case 3:
            color = 0xFFCC0000;
            break;
          case 4:
            color = 0xFF000088;
            break;
          case 5:
            color = 0xFF880000;
            break;
          default:
            color = 0xFF008888;
            break;
        }
        cell.label->SetColor(color);
      } else {
        cell.label->SetText("");
      }
    }
  }
}

void UpdateStatusDisplay() {
  int flags = 0;
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols; ++c) {
      if (cells[r][c].is_flagged) flags++;
    }
  }
  mine_counter_label->SetText("Mines: " + std::to_string(kMines));

  reset_button_label->SetText(ResetButtonString());
}

void RevealCell(int r, int c);

void FloodFill(int r, int c) {
  for (int dr = -1; dr <= 1; ++dr) {
    for (int dc = -1; dc <= 1; ++dc) {
      int nr = r + dr;
      int nc = c + dc;
      if (nr >= 0 && nr < kRows && nc >= 0 && nc < kCols) {
        if (!cells[nr][nc].is_revealed && !cells[nr][nc].is_flagged) {
          RevealCell(nr, nc);
        }
      }
    }
  }
}

void CheckWinCondition() {
  if (game_state != GameState::PLAYING) return;
  bool won = true;
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols; ++c) {
      if (!cells[r][c].is_mine && !cells[r][c].is_revealed) {
        won = false;
        break;
      }
    }
  }
  if (won) {
    game_state = GameState::WON;
    UpdateStatusDisplay();
  }
}

void RevealAllCells() {
  for (int ir = 0; ir < kRows; ++ir) {
    for (int ic = 0; ic < kCols; ++ic) {
      if (cells[ir][ic].is_mine) {
        cells[ir][ic].is_revealed = true;
        UpdateCellVisuals(ir, ic);
      }
    }
  }
}

void RevealCell(int r, int c) {
  Cell& cell = cells[r][c];
  if (cell.is_revealed || cell.is_flagged || game_state != GameState::PLAYING)
    return;

  cell.is_revealed = true;
  if (cell.is_mine) {
    game_state = GameState::LOST;
    RevealAllCells();
    UpdateStatusDisplay();
  } else {
    UpdateCellVisuals(r, c);
    if (cell.neighbor_mines == 0) FloodFill(r, c);
    CheckWinCondition();
  }
}

void ResetGame() {
  game_state = GameState::PLAYING;

  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols; ++c) {
      cells[r][c].is_mine = false;
      cells[r][c].is_revealed = false;
      cells[r][c].is_flagged = false;
      cells[r][c].neighbor_mines = 0;
    }
  }

  int placed = 0;
  while (placed < kMines) {
    int r = std::rand() % kRows;
    int c = std::rand() % kCols;
    if (!cells[r][c].is_mine) {
      cells[r][c].is_mine = true;
      placed++;
    }
  }

  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols; ++c) {
      if (cells[r][c].is_mine) continue;
      int count = 0;
      for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
          int nr = r + dr;
          int nc = c + dc;
          if (nr >= 0 && nr < kRows && nc >= 0 && nc < kCols) {
            if (cells[nr][nc].is_mine) count++;
          }
        }
      }
      cells[r][c].neighbor_mines = count;
    }
  }

  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols; ++c) {
      UpdateCellVisuals(r, c);
    }
  }
  UpdateStatusDisplay();
}

void OnCellClicked(int r, int c, perception::window::MouseButton button) {
  if (game_state != GameState::PLAYING) return;
  if (button == perception::window::MouseButton::Left) {
    RevealCell(r, c);
  } else if (button == perception::window::MouseButton::Right) {
    if (!cells[r][c].is_revealed) {
      cells[r][c].is_flagged = !cells[r][c].is_flagged;
      UpdateCellVisuals(r, c);
      UpdateStatusDisplay();
    }
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  std::srand(perception::RandomNumber());

  std::shared_ptr<Node> grid_container;
  game_state = GameState::PLAYING;

  auto window = UiWindow::DialogWithTitleBar(
      "Minesweeper",
      [](UiWindow& window) { window.OnClose([]() { TerminateProcess(); }); },
      Container::VerticalContainer(
          [](Layout& layout) { layout.SetAlignSelf(YGAlignStretch); },
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetAlignSelf(YGAlignStretch);
                layout.SetJustifyContent(YGJustifySpaceBetween);
                layout.SetAlignItems(YGAlignCenter);
              },
              Label::BasicLabel("Mines: " + std::to_string(kMines),
                                &mine_counter_label),
              Button::BasicButton(
                  ResetGame,
                  Label::BasicLabel(ResetButtonString(), &reset_button_label))),
          Container::VerticalContainer(
              [](Layout& layout) {
                layout.SetAlignSelf(YGAlignCenter);
                layout.SetGap(2.0f);
              },
              &grid_container)));

  for (int r = 0; r < kRows; ++r) {
    auto row_container = Container::HorizontalContainer(
        [](Layout& layout) { layout.SetGap(2.0f); });
    for (int c = 0; c < kCols; ++c) {
      auto cell_block = Block::SolidColor(
          0xFFCCCCCC,
          [](Layout& layout) {
            layout.SetWidth(30.0f);
            layout.SetHeight(30.0f);
            layout.SetJustifyContent(YGJustifyCenter);
            layout.SetAlignItems(YGAlignCenter);
          },
          [](Block& block) {
            block.SetBorderWidth(1.0f);
            block.SetBorderColor(0xFF888888);
            block.SetBorderRadius(2.0f);
          },
          [r, c](Node& node) {
            node.OnMouseButtonDown(
                [r, c](const perception::ui::Point& point,
                       perception::window::MouseButton button) {
                  OnCellClicked(r, c, button);
                });
          },
          Label::BasicLabel(
              "",
              [](Label& label) {
                label.SetTextAlignment(TextAlignment::MiddleCenter);
              },
              &cells[r][c].label),
          &cells[r][c].block);

      cells[r][c].node = cell_block;
      row_container->AddChild(cell_block);
    }
    grid_container->AddChild(row_container);
  }

  ResetGame();

  HandOverControl();
  return 0;
}
