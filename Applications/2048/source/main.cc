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
#include "perception/ui/components/focusable.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/keyboard.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"

using ::perception::HandOverControl;
using ::perception::TerminateProcess;
using ::perception::ui::KeyCode;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::Focusable;
using ::perception::ui::components::Label;
using ::perception::ui::components::UiWindow;

namespace {

int board[4][4];
int score = 0;

std::shared_ptr<Block> blocks[4][4];
std::shared_ptr<Label> labels[4][4];
std::shared_ptr<Label> score_label;

std::string ScoreString(int score) { return "Score: " + std::to_string(score); }

void ForEachTile(const std::function<void(int, int)>& on_each_tile) {
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      on_each_tile(r, c);
    }
  }
}
void UpdateVisuals() {
  ForEachTile([](int r, int c) {
    int val = board[r][c];
    uint32 bg = 0xFFCDC1B4;
    uint32 fg = 0xFF776E65;

    switch (val) {
      case 2:
        bg = 0xFFEEE4DA;
        break;
      case 4:
        bg = 0xFFEDE0C8;
        break;
      case 8:
        bg = 0xFFF2B179;
        fg = 0xFFF9F6F2;
        break;
      case 16:
        bg = 0xFFF59563;
        fg = 0xFFF9F6F2;
        break;
      case 32:
        bg = 0xFFF67C5F;
        fg = 0xFFF9F6F2;
        break;
      case 64:
        bg = 0xFFF65E3B;
        fg = 0xFFF9F6F2;
        break;
      case 128:
        bg = 0xFFEDCF72;
        fg = 0xFFF9F6F2;
        break;
      case 256:
        bg = 0xFFEDCC61;
        fg = 0xFFF9F6F2;
        break;
      case 512:
        bg = 0xFFEDC850;
        fg = 0xFFF9F6F2;
        break;
      case 1024:
        bg = 0xFFEDC53F;
        fg = 0xFFF9F6F2;
        break;
      case 2048:
        bg = 0xFFEDC22E;
        fg = 0xFFF9F6F2;
        break;
      default:
        if (val > 2048) {
          bg = 0xFF3C3A32;
          fg = 0xFFF9F6F2;
        }
        break;
    }

    blocks[r][c]->SetFillColor(bg);
    if (val > 0) {
      labels[r][c]->SetText(std::to_string(val));
      labels[r][c]->SetColor(fg);
    } else {
      labels[r][c]->SetText("");
    }
  });

  score_label->SetText(ScoreString(score));
}

void SpawnTile() {
  std::vector<std::pair<int, int>> empty;
  ForEachTile([&](int r, int c) {
    if (board[r][c] == 0) empty.push_back({r, c});
  });
  if (empty.empty()) return;

  int idx = std::rand() % empty.size();
  int val = (std::rand() % 10 == 0) ? 4 : 2;
  board[empty[idx].first][empty[idx].second] = val;
}

void NewGame() {
  score = 0;
  ForEachTile([](int r, int c) { board[r][c] = 0; });
  SpawnTile();
  SpawnTile();
  UpdateVisuals();
}

bool SlideArray(int line[4]) {
  bool moved = false;
  int temp[4] = {0};
  int idx = 0;

  for (int i = 0; i < 4; ++i) {
    if (line[i] != 0) {
      temp[idx++] = line[i];
      if (i != idx - 1) moved = true;
    }
  }

  for (int i = 0; i < 3; ++i) {
    if (temp[i] != 0 && temp[i] == temp[i + 1]) {
      temp[i] *= 2;
      score += temp[i];
      temp[i + 1] = 0;
      moved = true;
    }
  }

  int final_line[4] = {0};
  idx = 0;
  for (int i = 0; i < 4; ++i) {
    if (temp[i] != 0) {
      final_line[idx++] = temp[i];
    }
  }

  for (int i = 0; i < 4; ++i) {
    if (line[i] != final_line[i]) moved = true;
    line[i] = final_line[i];
  }

  return moved;
}

void Slide(int dx, int dy) {
  bool moved = false;

  if (dx == -1) {
    for (int r = 0; r < 4; ++r) {
      if (SlideArray(board[r])) moved = true;
    }
  } else if (dx == 1) {
    for (int r = 0; r < 4; ++r) {
      int line[4] = {board[r][3], board[r][2], board[r][1], board[r][0]};
      if (SlideArray(line)) moved = true;
      board[r][3] = line[0];
      board[r][2] = line[1];
      board[r][1] = line[2];
      board[r][0] = line[3];
    }
  } else if (dy == -1) {
    for (int c = 0; c < 4; ++c) {
      int line[4] = {board[0][c], board[1][c], board[2][c], board[3][c]};
      if (SlideArray(line)) moved = true;
      board[0][c] = line[0];
      board[1][c] = line[1];
      board[2][c] = line[2];
      board[3][c] = line[3];
    }
  } else if (dy == 1) {
    for (int c = 0; c < 4; ++c) {
      int line[4] = {board[3][c], board[2][c], board[1][c], board[0][c]};
      if (SlideArray(line)) moved = true;
      board[3][c] = line[0];
      board[2][c] = line[1];
      board[1][c] = line[2];
      board[0][c] = line[3];
    }
  }

  if (moved) {
    SpawnTile();
    UpdateVisuals();
  }
}

void HandleKeyDown(const perception::window::KeyboardKeyEvent& event) {
  switch (static_cast<KeyCode>(event.key)) {
    case KeyCode::UpArrow:
    case KeyCode::W:
      Slide(0, -1);
      break;
    case KeyCode::DownArrow:
    case KeyCode::S:
      Slide(0, 1);
      break;
    case KeyCode::LeftArrow:
    case KeyCode::A:
      Slide(-1, 0);
      break;
    case KeyCode::RightArrow:
    case KeyCode::D:
      Slide(1, 0);
      break;
    default:
      break;
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  std::srand(perception::RandomNumber());

  std::shared_ptr<Node> vertical_grid;
  std::shared_ptr<Node> focusable;

  auto window = UiWindow::DialogWithTitleBar(
      "2048",
      [](UiWindow& window) { window.OnClose([]() { TerminateProcess(); }); },
      Container::VerticalContainer(
          [](Layout& layout) { layout.SetAlignSelf(YGAlignStretch); },
          [](Focusable& focusable) { focusable.OnKeyDown(HandleKeyDown); },
          &focusable,
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetAlignSelf(YGAlignStretch);
                layout.SetJustifyContent(YGJustifySpaceBetween);
                layout.SetAlignItems(YGAlignCenter);
              },
              Label::BasicLabel("2048"),
              Label::BasicLabel("Score: 0", &score_label),
              Button::BasicButton(NewGame, Label::BasicLabel("New"))),
          Block::SolidColor(
              0xFFBBADA0,
              [](Layout& layout) { layout.SetAlignSelf(YGAlignCenter); },
              [](Block& block) { block.SetBorderRadius(6.0f); },
              Container::VerticalContainer(
                  [](Layout& layout) { layout.SetGap(8.0f); },
                  &vertical_grid))));

  for (int r = 0; r < 4; ++r) {
    auto row = Container::HorizontalContainer(
        [](Layout& layout) { layout.SetGap(8.0f); });
    for (int c = 0; c < 4; ++c) {
      auto cell = Block::SolidColor(
          0xFFCDC1B4,
          [](Layout& layout) {
            layout.SetWidth(50.0f);
            layout.SetHeight(50.0f);
            layout.SetJustifyContent(YGJustifyCenter);
            layout.SetAlignItems(YGAlignCenter);
          },
          [](Block& block) { block.SetBorderRadius(4.0f); },
          Label::BasicLabel(
              "",
              [](Label& label) {
                label.SetTextAlignment(TextAlignment::MiddleCenter);
              },
              &labels[r][c]),
          &blocks[r][c]);
      row->AddChild(cell);
    }
    vertical_grid->AddChild(row);
  }

  focusable->Get<Focusable>()->Focus();

  NewGame();

  HandOverControl();
  return 0;
}
