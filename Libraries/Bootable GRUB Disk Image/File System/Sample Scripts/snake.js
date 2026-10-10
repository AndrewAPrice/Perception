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

// Neon Arcade Snake for jsshell
// Run with: jsshell "/Sample Scripts/snake.js"

const GRID_WIDTH = 30;
const GRID_HEIGHT = 18;
const CELL_CHARS = 2;
const BOARD_INNER_WIDTH = GRID_WIDTH * CELL_CHARS; // 60 columns
const BOARD_OUTER_WIDTH = BOARD_INNER_WIDTH + 2;  // 62 columns
const TOTAL_UI_HEIGHT = GRID_HEIGHT + 4;          // HUD + top border + 18 rows + bottom border + controls = 22 rows

const INITIAL_TICK_MS = 135;
const MIN_TICK_MS = 50;
const SPEED_STEP_MS = 8;
const APPLES_PER_LEVEL = 5;
const BONUS_SPAWN_INTERVAL = 5;
const BONUS_DURATION_MS = 7500;

const ESC = "\x1b";
const SYNC_START = "\x1b[?2026h";
const SYNC_END = "\x1b[?2026l";
const RESET = "\x1b[0m";
const BOLD = "\x1b[1m";
const DIM = "\x1b[2m";

function fgRgb(r, g, b) {
  return `\x1b[38;2;${r};${g};${b}m`;
}

function bgRgb(r, g, b) {
  return `\x1b[48;2;${r};${g};${b}m`;
}

function moveTo(row, col) {
  return `\x1b[${row};${col}H`;
}

const COLOR_BORDER = fgRgb(0, 240, 255);
const COLOR_HUD_LABEL = fgRgb(130, 160, 195);
const COLOR_HUD_VALUE = BOLD + fgRgb(255, 255, 255);
const COLOR_HIGH_SCORE = BOLD + fgRgb(255, 215, 0);
const COLOR_LEVEL = BOLD + fgRgb(0, 255, 180);
const COLOR_APPLE = BOLD + fgRgb(255, 55, 95);
const COLOR_BONUS = BOLD + fgRgb(255, 215, 40);
const COLOR_GRID_DOT = fgRgb(24, 32, 48);
const COLOR_BG = bgRgb(10, 14, 24);
const COLOR_OVERLAY_BG = bgRgb(16, 22, 38);

function nowMs() {
  return Math.floor(sys.uptime());
}

function padRight(str, len) {
  if (str.length >= len) return str.slice(0, len);
  return str + " ".repeat(len - str.length);
}

function padLeft(str, len) {
  if (str.length >= len) return str.slice(0, len);
  return " ".repeat(len - str.length) + str;
}

function centerText(str, width) {
  if (str.length >= width) return str.slice(0, width);
  const left = Math.floor((width - str.length) / 2);
  const right = width - str.length - left;
  return " ".repeat(left) + str + " ".repeat(right);
}

function snakeSegmentStyle(index, length) {
  if (index === 0) {
    return {
      color: BOLD + fgRgb(80, 255, 120),
      glyph: "██",
    };
  }
  const t = length > 1 ? index / (length - 1) : 0;
  const r = Math.round(20 * (1 - t) + 15 * t);
  const g = Math.round(235 * (1 - t) + 145 * t);
  const b = Math.round(130 * (1 - t) + 255 * t);
  let glyph = "██";
  if (index === length - 1 && length > 3) {
    glyph = "▒▒";
  } else if (t > 0.72) {
    glyph = "▓▓";
  }
  return {
    color: fgRgb(r, g, b),
    glyph,
  };
}

function decodeKeyAction(key) {
  if (!key) return null;
  if (
    key === "q" ||
    key === "Q" ||
    key === "Escape" ||
    key === "Esc" ||
    key === "\x1b" ||
    key === "Ctrl+C" ||
    key === "\x03"
  ) {
    return { type: "quit" };
  }
  if (key === "p" || key === "P" || key === " " || key === "Space") {
    return { type: "pause" };
  }
  if (
    key === "r" ||
    key === "R" ||
    key === "Enter" ||
    key === "\r" ||
    key === "\n"
  ) {
    return { type: "restart" };
  }
  if (
    key === "ArrowUp" ||
    key === "Up" ||
    key === "\x1b[A" ||
    key === "w" ||
    key === "W" ||
    key === "k" ||
    key === "K"
  ) {
    return { type: "dir", dx: 0, dy: -1 };
  }
  if (
    key === "ArrowDown" ||
    key === "Down" ||
    key === "\x1b[B" ||
    key === "s" ||
    key === "S" ||
    key === "j" ||
    key === "J"
  ) {
    return { type: "dir", dx: 0, dy: 1 };
  }
  if (
    key === "ArrowLeft" ||
    key === "Left" ||
    key === "\x1b[D" ||
    key === "a" ||
    key === "A" ||
    key === "h" ||
    key === "H"
  ) {
    return { type: "dir", dx: -1, dy: 0 };
  }
  if (
    key === "ArrowRight" ||
    key === "Right" ||
    key === "\x1b[C" ||
    key === "d" ||
    key === "D" ||
    key === "l" ||
    key === "L"
  ) {
    return { type: "dir", dx: 1, dy: 0 };
  }
  return null;
}

async function runGame() {
  term.altScreen(true);
  term.rawMode(true);
  term.cursor(false);
  term.title("Neon Snake — jsshell");

  try {
    let highScore = 0;
    let snake = [];
    let dir = { dx: 1, dy: 0 };
    let turnQueue = [];
    let apple = { x: 0, y: 0 };
    let bonusFruit = null; // { x, y, expiresAtMs }
    let score = 0;
    let applesEaten = 0;
    let level = 1;
    let paused = false;
    let gameOver = false;
    let pauseStartedMs = 0;
    let lastCols = 0;
    let lastRows = 0;

    function isCellOccupied(x, y) {
      for (let i = 0; i < snake.length; i++) {
        if (snake[i].x === x && snake[i].y === y) return true;
      }
      if (apple && apple.x === x && apple.y === y) return true;
      if (bonusFruit && bonusFruit.x === x && bonusFruit.y === y) return true;
      return false;
    }

    function randomFreeCell() {
      const free = [];
      for (let y = 0; y < GRID_HEIGHT; y++) {
        for (let x = 0; x < GRID_WIDTH; x++) {
          if (!isCellOccupied(x, y)) {
            free.push({ x, y });
          }
        }
      }
      if (free.length === 0) return { x: 0, y: 0 };
      return free[Math.floor(Math.random() * free.length)];
    }

    function resetMatch() {
      const startX = Math.floor(GRID_WIDTH / 2);
      const startY = Math.floor(GRID_HEIGHT / 2);
      snake = [
        { x: startX, y: startY },
        { x: startX - 1, y: startY },
        { x: startX - 2, y: startY },
        { x: startX - 3, y: startY },
      ];
      dir = { dx: 1, dy: 0 };
      turnQueue = [];
      apple = { x: -1, y: -1 };
      bonusFruit = null;
      score = 0;
      applesEaten = 0;
      level = 1;
      paused = false;
      gameOver = false;
      apple = randomFreeCell();
    }

    function currentTickMs() {
      return Math.max(
        MIN_TICK_MS,
        INITIAL_TICK_MS - (level - 1) * SPEED_STEP_MS
      );
    }

    function enqueueDirection(dx, dy) {
      if (turnQueue.length >= 2) return;
      const refDir =
        turnQueue.length > 0 ? turnQueue[turnQueue.length - 1] : dir;
      // Ignore identical direction or 180-degree self-reversal.
      if (refDir.dx === dx && refDir.dy === dy) return;
      if (refDir.dx + dx === 0 && refDir.dy + dy === 0) return;
      turnQueue.push({ dx, dy });
    }

    function stepSimulation(currentTimeMs) {
      if (bonusFruit && currentTimeMs >= bonusFruit.expiresAtMs) {
        bonusFruit = null;
      }

      if (turnQueue.length > 0) {
        dir = turnQueue.shift();
      }

      const nextHead = {
        x: snake[0].x + dir.dx,
        y: snake[0].y + dir.dy,
      };

      // Check wall collision.
      if (
        nextHead.x < 0 ||
        nextHead.x >= GRID_WIDTH ||
        nextHead.y < 0 ||
        nextHead.y >= GRID_HEIGHT
      ) {
        gameOver = true;
        return;
      }

      const eatingApple = nextHead.x === apple.x && nextHead.y === apple.y;
      const eatingBonus =
        bonusFruit !== null &&
        nextHead.x === bonusFruit.x &&
        nextHead.y === bonusFruit.y;

      // Tail moves forward unless the snake grows this tick.
      const checkLength =
        eatingApple || eatingBonus ? snake.length : snake.length - 1;
      for (let i = 0; i < checkLength; i++) {
        if (snake[i].x === nextHead.x && snake[i].y === nextHead.y) {
          gameOver = true;
          return;
        }
      }

      snake.unshift(nextHead);

      if (eatingApple) {
        applesEaten++;
        level = 1 + Math.floor(applesEaten / APPLES_PER_LEVEL);
        score += 10 * level;
        if (score > highScore) highScore = score;
        apple = randomFreeCell();

        if (
          applesEaten % BONUS_SPAWN_INTERVAL === 0 &&
          bonusFruit === null
        ) {
          const cell = randomFreeCell();
          bonusFruit = {
            x: cell.x,
            y: cell.y,
            expiresAtMs: currentTimeMs + BONUS_DURATION_MS,
          };
        }
      } else if (eatingBonus) {
        const remainingRatio = Math.max(
          0.2,
          (bonusFruit.expiresAtMs - currentTimeMs) / BONUS_DURATION_MS
        );
        const bonusPoints = Math.round(50 * level * remainingRatio);
        score += bonusPoints;
        if (score > highScore) highScore = score;
        bonusFruit = null;
      } else {
        snake.pop();
      }
    }

    function renderFrame(currentTimeMs) {
      const size = (term && typeof term.size === "function" && term.size()) || {
        cols: 80,
        rows: 24,
      };
      const cols = size.cols || 80;
      const rows = size.rows || 24;

      let out = SYNC_START;
      if (cols !== lastCols || rows !== lastRows) {
        out += "\x1b[2J";
        lastCols = cols;
        lastRows = rows;
      }

      const startCol = Math.max(
        1,
        Math.floor((cols - BOARD_OUTER_WIDTH) / 2) + 1
      );
      const startRow = Math.max(
        1,
        Math.floor((rows - TOTAL_UI_HEIGHT) / 2) + 1
      );

      // Top HUD bar.
      const scoreText = padLeft(String(score), 5);
      const highText = padLeft(String(highScore), 5);
      const levelText = padLeft(String(level), 2);
      const lenText = padLeft(String(snake.length), 3);

      let bonusHud = "            ";
      if (bonusFruit) {
        const remMs = Math.max(
          0,
          bonusFruit.expiresAtMs - (paused ? pauseStartedMs : currentTimeMs)
        );
        const bars = Math.max(
          1,
          Math.min(6, Math.ceil((remMs / BONUS_DURATION_MS) * 6))
        );
        bonusHud =
          COLOR_BONUS +
          "◆◆ " +
          "▰".repeat(bars) +
          DIM +
          "▱".repeat(6 - bars) +
          RESET +
          "   ";
      }

      out +=
        moveTo(startRow, startCol) +
        COLOR_HUD_LABEL +
        "SCORE " +
        COLOR_HUD_VALUE +
        scoreText +
        RESET +
        "  " +
        COLOR_HUD_LABEL +
        "HIGH " +
        COLOR_HIGH_SCORE +
        highText +
        RESET +
        "  " +
        COLOR_HUD_LABEL +
        "LVL " +
        COLOR_LEVEL +
        levelText +
        RESET +
        "  " +
        COLOR_HUD_LABEL +
        "LEN " +
        COLOR_HUD_VALUE +
        lenText +
        RESET +
        "  " +
        bonusHud;

      // Top border with title badge.
      const titleBadge = "╡ NEON SNAKE ╞";
      const leftBorderLen = Math.floor(
        (BOARD_INNER_WIDTH - titleBadge.length) / 2
      );
      const rightBorderLen =
        BOARD_INNER_WIDTH - titleBadge.length - leftBorderLen;
      out +=
        moveTo(startRow + 1, startCol) +
        COLOR_BORDER +
        "╔" +
        "═".repeat(leftBorderLen) +
        BOLD +
        fgRgb(0, 255, 200) +
        titleBadge +
        RESET +
        COLOR_BORDER +
        "═".repeat(rightBorderLen) +
        "╗" +
        RESET;

      // Build lookup map for snake segments.
      const cellMap = new Map();
      for (let i = snake.length - 1; i >= 0; i--) {
        cellMap.set(`${snake[i].x},${snake[i].y}`, i);
      }

      // Render 18 playfield rows.
      for (let y = 0; y < GRID_HEIGHT; y++) {
        let rowStr =
          moveTo(startRow + 2 + y, startCol) +
          COLOR_BORDER +
          "║" +
          COLOR_BG;
        for (let x = 0; x < GRID_WIDTH; x++) {
          const segIndex = cellMap.get(`${x},${y}`);
          if (segIndex !== undefined) {
            const seg = snakeSegmentStyle(segIndex, snake.length);
            rowStr += seg.color + seg.glyph + RESET + COLOR_BG;
          } else if (apple.x === x && apple.y === y) {
            rowStr += COLOR_APPLE + "●●" + RESET + COLOR_BG;
          } else if (bonusFruit && bonusFruit.x === x && bonusFruit.y === y) {
            rowStr += COLOR_BONUS + "◆◆" + RESET + COLOR_BG;
          } else {
            rowStr += COLOR_GRID_DOT + " ·";
          }
        }
        rowStr += RESET + COLOR_BORDER + "║" + RESET;
        out += rowStr;
      }

      // Bottom border.
      out +=
        moveTo(startRow + 2 + GRID_HEIGHT, startCol) +
        COLOR_BORDER +
        "╚" +
        "═".repeat(BOARD_INNER_WIDTH) +
        "╝" +
        RESET;

      // Bottom controls bar.
      const controlsText =
        "[Arrows/WASD/HJKL] Move  [P/Space] Pause  [R] Restart  [Q/Esc] Quit";
      out +=
        moveTo(startRow + 3 + GRID_HEIGHT, startCol) +
        COLOR_HUD_LABEL +
        padRight(controlsText, BOARD_OUTER_WIDTH) +
        RESET;

      // Pause or Game Over centered overlay box.
      if (paused || gameOver) {
        const boxWidth = 36;
        const boxCol = startCol + Math.floor((BOARD_OUTER_WIDTH - boxWidth) / 2);
        const boxRow = startRow + 2 + Math.floor((GRID_HEIGHT - 6) / 2);
        const borderColor = gameOver
          ? BOLD + fgRgb(255, 70, 100)
          : BOLD + fgRgb(0, 240, 255);

        const titleLine = gameOver ? "G A M E   O V E R" : "P A U S E D";
        const statsLine = gameOver
          ? `Score: ${score}   High: ${highScore}`
          : `Level ${level}  •  Length ${snake.length}`;
        const promptLine1 = gameOver
          ? "[R / Enter] Play Again"
          : "[P / Space] Resume Game";
        const promptLine2 = "[Q / Esc]   Quit to Shell";

        out +=
          moveTo(boxRow, boxCol) +
          COLOR_OVERLAY_BG +
          borderColor +
          "╔" +
          "═".repeat(boxWidth - 2) +
          "╗" +
          RESET;
        out +=
          moveTo(boxRow + 1, boxCol) +
          COLOR_OVERLAY_BG +
          borderColor +
          "║" +
          COLOR_HUD_VALUE +
          centerText(titleLine, boxWidth - 2) +
          borderColor +
          "║" +
          RESET;
        out +=
          moveTo(boxRow + 2, boxCol) +
          COLOR_OVERLAY_BG +
          borderColor +
          "║" +
          COLOR_HIGH_SCORE +
          centerText(statsLine, boxWidth - 2) +
          borderColor +
          "║" +
          RESET;
        out +=
          moveTo(boxRow + 3, boxCol) +
          COLOR_OVERLAY_BG +
          borderColor +
          "║" +
          " ".repeat(boxWidth - 2) +
          "║" +
          RESET;
        out +=
          moveTo(boxRow + 4, boxCol) +
          COLOR_OVERLAY_BG +
          borderColor +
          "║" +
          COLOR_LEVEL +
          centerText(promptLine1, boxWidth - 2) +
          borderColor +
          "║" +
          RESET;
        out +=
          moveTo(boxRow + 5, boxCol) +
          COLOR_OVERLAY_BG +
          borderColor +
          "║" +
          COLOR_HUD_LABEL +
          centerText(promptLine2, boxWidth - 2) +
          borderColor +
          "║" +
          RESET;
        out +=
          moveTo(boxRow + 6, boxCol) +
          COLOR_OVERLAY_BG +
          borderColor +
          "╚" +
          "═".repeat(boxWidth - 2) +
          "╝" +
          RESET;
      }

      out += SYNC_END;
      term.write(out);
    }

    resetMatch();
    let nextTickDeadlineMs = nowMs() + currentTickMs();
    renderFrame(nowMs());

    while (true) {
      const now = nowMs();
      const waitMs =
        paused || gameOver ? 100 : Math.max(1, nextTickDeadlineMs - now);

      const rawKey = await term.readKey(waitMs);
      const afterReadMs = nowMs();

      if (rawKey) {
        const action = decodeKeyAction(rawKey);
        if (action) {
          if (action.type === "quit") {
            break;
          } else if (action.type === "restart") {
            resetMatch();
            nextTickDeadlineMs = afterReadMs + currentTickMs();
            renderFrame(afterReadMs);
            continue;
          } else if (action.type === "pause") {
            if (gameOver) {
              resetMatch();
              nextTickDeadlineMs = afterReadMs + currentTickMs();
            } else if (!paused) {
              paused = true;
              pauseStartedMs = afterReadMs;
            } else {
              const pausedDuration = afterReadMs - pauseStartedMs;
              if (bonusFruit) {
                bonusFruit.expiresAtMs += pausedDuration;
              }
              paused = false;
              nextTickDeadlineMs = afterReadMs + currentTickMs();
            }
            renderFrame(afterReadMs);
            continue;
          } else if (action.type === "dir" && !paused && !gameOver) {
            enqueueDirection(action.dx, action.dy);
          }
        }
      }

      const tickNow = nowMs();
      if (!paused && !gameOver && tickNow >= nextTickDeadlineMs) {
        stepSimulation(tickNow);
        nextTickDeadlineMs = tickNow + currentTickMs();
        renderFrame(tickNow);
      }
    }
  } finally {
    term.cursor(true);
    term.rawMode(false);
    term.altScreen(false);
  }
}

runGame();
