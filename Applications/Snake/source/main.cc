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

#include <chrono>
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
#include "perception/time.h"
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

constexpr int kGridSize = 16;
std::shared_ptr<Block> blocks[kGridSize][kGridSize];
std::shared_ptr<Label> score_label;

std::vector<std::pair<int, int>> snake;
std::pair<int, int> apple;

enum class Directions { Up, Down, Left, Right };
Directions direction = Directions::Right;

bool game_over = false;
int score = 0;

void SpawnApple() {
  std::vector<std::pair<int, int>> empty;
  for (int x = 0; x < kGridSize; ++x) {
    for (int y = 0; y < kGridSize; ++y) {
      bool in_snake = false;
      for (const auto& segment : snake) {
        if (segment.first == x && segment.second == y) {
          in_snake = true;
          break;
        }
      }
      if (!in_snake) empty.push_back({x, y});
    }
  }
  if (empty.empty()) return;
  apple = empty[std::rand() % empty.size()];
}

void RenderGrid() {
  for (int y = 0; y < kGridSize; ++y) {
    for (int x = 0; x < kGridSize; ++x) {
      blocks[y][x]->SetFillColor(0xFF111111);
    }
  }

  blocks[apple.second][apple.first]->SetFillColor(0xFFFF0000);

  for (size_t i = 0; i < snake.size(); ++i) {
    int sx = snake[i].first;
    int sy = snake[i].second;
    if (sx >= 0 && sx < kGridSize && sy >= 0 && sy < kGridSize) {
      blocks[sy][sx]->SetFillColor(i == 0 ? 0xFF00FF00 : 0xFF00AA00);
    }
  };

  std::string score_string = "Score: " + std::to_string(score);
  if (game_over) score_string += " - Game Over";
  score_label->SetText(score_string);
}

void GameLoop() {
  if (game_over) return;

  int nx = snake.front().first;
  int ny = snake.front().second;

  switch (direction) {
    case Directions::Up:
      ny--;
      break;
    case Directions::Down:
      ny++;
      break;
    case Directions::Left:
      nx--;
      break;
    case Directions::Right:
      nx++;
      break;
  }

  if (nx < 0 || nx >= kGridSize || ny < 0 || ny >= kGridSize) {
    game_over = true;
    RenderGrid();
    return;
  }

  for (size_t i = 0; i < snake.size() - 1; ++i) {
    if (snake[i].first == nx && snake[i].second == ny) {
      game_over = true;
      RenderGrid();
      return;
    }
  }

  snake.insert(snake.begin(), {nx, ny});

  if (nx == apple.first && ny == apple.second) {
    score++;
    SpawnApple();
  } else {
    snake.pop_back();
  }

  RenderGrid();

  perception::AfterDuration(std::chrono::milliseconds(150), GameLoop);
}

void StartGame() {
  snake.clear();
  snake.push_back({kGridSize / 2, kGridSize / 2});
  snake.push_back({kGridSize / 2 - 1, kGridSize / 2});
  snake.push_back({kGridSize / 2 - 2, kGridSize / 2});

  direction = Directions::Right;
  score = 0;
  game_over = false;

  SpawnApple();
  RenderGrid();

  perception::AfterDuration(std::chrono::milliseconds(150), GameLoop);
}

void HandleKeyDown(const perception::window::KeyboardKeyEvent& event) {
  switch (static_cast<KeyCode>(event.key)) {
    case KeyCode::UpArrow:
    case KeyCode::W:
      if (direction != Directions::Down) direction = Directions::Up;
      break;
    case KeyCode::DownArrow:
    case KeyCode::S:
      if (direction != Directions::Up) direction = Directions::Down;
      break;
    case KeyCode::LeftArrow:
    case KeyCode::A:
      if (direction != Directions::Right) direction = Directions::Left;
      break;
    case KeyCode::RightArrow:
    case KeyCode::D:
      if (direction != Directions::Left) direction = Directions::Right;
      break;
    default:
      break;
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  std::srand(perception::RandomNumber());

  std::shared_ptr<Node> vertical_grid;
  std::shared_ptr<Focusable> main_container_focusable;

  auto window = UiWindow::DialogWithTitleBar(
      "Snake",
      [](UiWindow& window) { window.OnClose([]() { TerminateProcess(); }); },
      Container::VerticalContainer(
          [](Layout& layout) { layout.SetAlignSelf(YGAlignStretch); },
          [](Focusable& focusable) { focusable.OnKeyDown(HandleKeyDown); },
          &main_container_focusable,
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetAlignSelf(YGAlignStretch);
                layout.SetJustifyContent(YGJustifySpaceBetween);
                layout.SetAlignItems(YGAlignCenter);
              },
              Label::BasicLabel("Score: 0", &score_label),
              Button::BasicButton(StartGame, Label::BasicLabel("Reset"))),
          Block::SolidColor(
              0xFF333333,
              [](Layout& layout) {
                layout.SetAlignSelf(YGAlignCenter);
                layout.SetPadding(YGEdgeAll, 4.0f);
              },
              [](Block& block) { block.SetBorderRadius(4.0f); },
              Container::VerticalContainer(
                  [](Layout& layout) { layout.SetGap(1.0f); },
                  &vertical_grid))));

  main_container_focusable->Focus();

  for (int y = 0; y < kGridSize; ++y) {
    auto row = Container::HorizontalContainer(
        [](Layout& layout) { layout.SetGap(1.0f); });
    for (int x = 0; x < kGridSize; ++x) {
      auto cell = Block::SolidColor(
          0xFF111111,
          [](Layout& layout) {
            layout.SetWidth(16.0f);
            layout.SetHeight(16.0f);
          },
          [](Block& block) { block.SetBorderRadius(2.0f); }, &blocks[y][x]);
      row->AddChild(cell);
    }
    vertical_grid->AddChild(row);
  }

  StartGame();

  HandOverControl();
  return 0;
}
