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

// 2048 — Sliding Tile Puzzle for jsshell
// Run via `/run "/Sample Scripts/2048.js"` or `jsshell "/Sample Scripts/2048.js"`

(async () => {
  const SYNC_START = "\x1b[?2026h";
  const SYNC_END = "\x1b[?2026l";
  const RESET = "\x1b[0m";
  const BOLD = "\x1b[1m";

  function fgRgb(r, g, b) {
    return `\x1b[38;2;${r};${g};${b}m`;
  }

  function bgRgb(r, g, b) {
    return `\x1b[48;2;${r};${g};${b}m`;
  }

  // Authentic 24-bit RGB palette for empty cells and 2048 tiles.
  const TILE_PALETTE = {
    0: { bg: [58, 52, 46], fg: [119, 110, 101] },
    2: { bg: [238, 228, 218], fg: [119, 110, 101] },
    4: { bg: [237, 224, 200], fg: [119, 110, 101] },
    8: { bg: [242, 177, 121], fg: [249, 246, 242] },
    16: { bg: [245, 149, 99], fg: [249, 246, 242] },
    32: { bg: [246, 124, 95], fg: [249, 246, 242] },
    64: { bg: [246, 94, 59], fg: [249, 246, 242] },
    128: { bg: [237, 207, 114], fg: [60, 54, 46] },
    256: { bg: [237, 204, 97], fg: [60, 54, 46] },
    512: { bg: [237, 200, 80], fg: [60, 54, 46] },
    1024: { bg: [237, 197, 63], fg: [249, 246, 242] },
    2048: { bg: [237, 194, 46], fg: [249, 246, 242] },
    super: { bg: [60, 58, 50], fg: [249, 246, 242] },
  };

  const BORDER_FG = fgRgb(187, 173, 160);
  const PANEL_BORDER_FG = fgRgb(120, 112, 104);
  const LABEL_FG = fgRgb(186, 178, 168);
  const VALUE_FG = fgRgb(249, 246, 242);
  const ACCENT_FG = fgRgb(237, 194, 46);
  const DELTA_FG = fgRgb(166, 227, 161);
  const KEY_FG = fgRgb(137, 180, 250);
  const MUTED_FG = fgRgb(140, 132, 124);

  function tileColors(value) {
    if (TILE_PALETTE[value]) return TILE_PALETTE[value];
    return TILE_PALETTE.super;
  }

  function centerText(text, width) {
    const str = String(text);
    if (str.length >= width) return str.slice(0, width);
    const totalPad = width - str.length;
    const leftPad = Math.floor(totalPad / 2);
    const rightPad = totalPad - leftPad;
    return " ".repeat(leftPad) + str + " ".repeat(rightPad);
  }

  function padRight(text, width) {
    const str = String(text);
    if (str.length >= width) return str.slice(0, width);
    return str + " ".repeat(width - str.length);
  }

  function padLeft(text, width) {
    const str = String(text);
    if (str.length >= width) return str.slice(0, width);
    return " ".repeat(width - str.length) + str;
  }

  function createEmptyBoard() {
    return [
      [0, 0, 0, 0],
      [0, 0, 0, 0],
      [0, 0, 0, 0],
      [0, 0, 0, 0],
    ];
  }

  function cloneBoard(b) {
    return b.map((row) => row.slice());
  }

  function createHighlightMask() {
    return [
      [false, false, false, false],
      [false, false, false, false],
      [false, false, false, false],
      [false, false, false, false],
    ];
  }

  let board = createEmptyBoard();
  let highlights = createHighlightMask();
  let score = 0;
  let bestScore = 0;
  let moves = 0;
  let lastDelta = 0;
  let hasWon = false;
  let endlessMode = false;
  let gameOver = false;
  let history = [];
  const startUptimeMs =
    typeof sys !== "undefined" && typeof sys.uptime === "function"
      ? sys.uptime()
      : Date.now();

  function getMaxTile() {
    let maxVal = 0;
    for (let r = 0; r < 4; ++r) {
      for (let c = 0; c < 4; ++c) {
        if (board[r][c] > maxVal) maxVal = board[r][c];
      }
    }
    return maxVal;
  }

  function spawnRandomTile() {
    const emptyCells = [];
    for (let r = 0; r < 4; ++r) {
      for (let c = 0; c < 4; ++c) {
        if (board[r][c] === 0) emptyCells.push([r, c]);
      }
    }
    if (emptyCells.length === 0) return false;
    const [r, c] = emptyCells[Math.floor(Math.random() * emptyCells.length)];
    board[r][c] = Math.random() < 0.9 ? 2 : 4;
    highlights[r][c] = true;
    return true;
  }

  function hasAvailableMoves() {
    for (let r = 0; r < 4; ++r) {
      for (let c = 0; c < 4; ++c) {
        if (board[r][c] === 0) return true;
        if (c < 3 && board[r][c] === board[r][c + 1]) return true;
        if (r < 3 && board[r][c] === board[r + 1][c]) return true;
      }
    }
    return false;
  }

  function resetGame() {
    board = createEmptyBoard();
    highlights = createHighlightMask();
    score = 0;
    moves = 0;
    lastDelta = 0;
    hasWon = false;
    endlessMode = false;
    gameOver = false;
    history = [];
    spawnRandomTile();
    spawnRandomTile();
  }

  // Slides a 4-element line toward index 0, enforcing single-merge-per-tile-per-turn.
  function slideLine(line) {
    const nonZero = [];
    for (let i = 0; i < 4; ++i) {
      if (line[i] !== 0) nonZero.push(line[i]);
    }

    const result = [0, 0, 0, 0];
    const mergedIndices = [false, false, false, false];
    let delta = 0;
    let writeIdx = 0;
    let readIdx = 0;

    while (readIdx < nonZero.length) {
      if (
        readIdx + 1 < nonZero.length &&
        nonZero[readIdx] === nonZero[readIdx + 1]
      ) {
        const mergedVal = nonZero[readIdx] * 2;
        result[writeIdx] = mergedVal;
        mergedIndices[writeIdx] = true;
        delta += mergedVal;
        readIdx += 2;
      } else {
        result[writeIdx] = nonZero[readIdx];
        readIdx += 1;
      }
      writeIdx += 1;
    }

    let moved = false;
    for (let i = 0; i < 4; ++i) {
      if (line[i] !== result[i]) {
        moved = true;
        break;
      }
    }

    return { result, mergedIndices, delta, moved };
  }

  function slide(direction) {
    if (gameOver || (hasWon && !endlessMode)) return false;

    const prevBoard = cloneBoard(board);
    const prevScore = score;
    const prevMoves = moves;
    const prevHasWon = hasWon;
    const prevEndlessMode = endlessMode;

    const nextHighlights = createHighlightMask();
    let anyMoved = false;
    let turnDelta = 0;

    if (direction === "left") {
      for (let r = 0; r < 4; ++r) {
        const { result, mergedIndices, delta, moved } = slideLine(board[r]);
        if (moved) anyMoved = true;
        turnDelta += delta;
        board[r] = result;
        for (let c = 0; c < 4; ++c) {
          if (mergedIndices[c]) nextHighlights[r][c] = true;
        }
      }
    } else if (direction === "right") {
      for (let r = 0; r < 4; ++r) {
        const reversed = [board[r][3], board[r][2], board[r][1], board[r][0]];
        const { result, mergedIndices, delta, moved } = slideLine(reversed);
        if (moved) anyMoved = true;
        turnDelta += delta;
        for (let i = 0; i < 4; ++i) {
          board[r][3 - i] = result[i];
          if (mergedIndices[i]) nextHighlights[r][3 - i] = true;
        }
      }
    } else if (direction === "up") {
      for (let c = 0; c < 4; ++c) {
        const col = [board[0][c], board[1][c], board[2][c], board[3][c]];
        const { result, mergedIndices, delta, moved } = slideLine(col);
        if (moved) anyMoved = true;
        turnDelta += delta;
        for (let r = 0; r < 4; ++r) {
          board[r][c] = result[r];
          if (mergedIndices[r]) nextHighlights[r][c] = true;
        }
      }
    } else if (direction === "down") {
      for (let c = 0; c < 4; ++c) {
        const col = [board[3][c], board[2][c], board[1][c], board[0][c]];
        const { result, mergedIndices, delta, moved } = slideLine(col);
        if (moved) anyMoved = true;
        turnDelta += delta;
        for (let i = 0; i < 4; ++i) {
          board[3 - i][c] = result[i];
          if (mergedIndices[i]) nextHighlights[3 - i][c] = true;
        }
      }
    }

    if (!anyMoved) return false;

    history.push({
      board: prevBoard,
      score: prevScore,
      moves: prevMoves,
      hasWon: prevHasWon,
      endlessMode: prevEndlessMode,
    });
    if (history.length > 256) history.shift();

    highlights = nextHighlights;
    score += turnDelta;
    lastDelta = turnDelta;
    moves += 1;
    if (score > bestScore) bestScore = score;

    spawnRandomTile();

    if (!hasWon && getMaxTile() >= 2048) {
      hasWon = true;
    }
    if (!hasAvailableMoves()) {
      gameOver = true;
    }
    return true;
  }

  function undoMove() {
    if (history.length === 0) return false;
    const prev = history.pop();
    board = prev.board;
    score = prev.score;
    moves = prev.moves;
    hasWon = prev.hasWon;
    endlessMode = prev.endlessMode;
    lastDelta = 0;
    gameOver = false;
    highlights = createHighlightMask();
    return true;
  }

  function getTerminalSize() {
    let cols = 80;
    let rows = 24;
    if (typeof term !== "undefined" && typeof term.size === "function") {
      const sz = term.size();
      if (sz && typeof sz === "object") {
        if (typeof sz.cols === "number" && sz.cols > 0) cols = sz.cols;
        if (typeof sz.rows === "number" && sz.rows > 0) rows = sz.rows;
      }
    }
    return { cols, rows };
  }

  function renderTileLine(r, c, subRow) {
    const val = board[r][c];
    const isHighlighted = highlights[r][c];
    const { bg, fg } = tileColors(val);
    const style = bgRgb(bg[0], bg[1], bg[2]) + fgRgb(fg[0], fg[1], fg[2]);

    if (subRow === 0) {
      const badge = isHighlighted && val > 0 ? "      *" : "       ";
      return style + badge + RESET;
    }
    if (subRow === 1) {
      const label = val > 0 ? centerText(val, 7) : "   ·   ";
      return style + (val > 0 ? BOLD : "") + label + RESET;
    }
    return style + "       " + RESET;
  }

  // Builds the 17-row 4x4 board (each tile is 7 cols x 3 rows, with box-drawing borders).
  function buildBoardLines() {
    const lines = [];
    lines.push(`${BORDER_FG}╭───────┬───────┬───────┬───────╮${RESET}`);
    for (let r = 0; r < 4; ++r) {
      for (let sub = 0; sub < 3; ++sub) {
        let rowStr = `${BORDER_FG}│${RESET}`;
        for (let c = 0; c < 4; ++c) {
          rowStr += renderTileLine(r, c, sub) + `${BORDER_FG}│${RESET}`;
        }
        lines.push(rowStr);
      }
      if (r < 3) {
        lines.push(`${BORDER_FG}├───────┼───────┼───────┼───────┤${RESET}`);
      }
    }
    lines.push(`${BORDER_FG}╰───────┴───────┴───────┴───────╯${RESET}`);

    // Overlay Victory or Game Over banner across the center of the board when active.
    if (hasWon && !endlessMode) {
      const modalBg = bgRgb(237, 194, 46) + fgRgb(40, 36, 30) + BOLD;
      lines[6] = `${BORDER_FG}│${RESET}${modalBg}╔═════════════════════════════╗${RESET}${BORDER_FG}│${RESET}`;
      lines[7] = `${BORDER_FG}│${RESET}${modalBg}║     ★  YOU REACHED 2048! ★  ║${RESET}${BORDER_FG}│${RESET}`;
      lines[8] = `${BORDER_FG}│${RESET}${modalBg}║                             ║${RESET}${BORDER_FG}│${RESET}`;
      lines[9] = `${BORDER_FG}│${RESET}${modalBg}║  [C] Continue Endless Mode  ║${RESET}${BORDER_FG}│${RESET}`;
      lines[10] = `${BORDER_FG}│${RESET}${modalBg}║  [R] Restart   [U] Undo     ║${RESET}${BORDER_FG}│${RESET}`;
      lines[11] = `${BORDER_FG}│${RESET}${modalBg}╚═════════════════════════════╝${RESET}${BORDER_FG}│${RESET}`;
    } else if (gameOver) {
      const modalBg = bgRgb(180, 62, 52) + fgRgb(249, 246, 242) + BOLD;
      lines[6] = `${BORDER_FG}│${RESET}${modalBg}╔═════════════════════════════╗${RESET}${BORDER_FG}│${RESET}`;
      lines[7] = `${BORDER_FG}│${RESET}${modalBg}║         GAME  OVER          ║${RESET}${BORDER_FG}│${RESET}`;
      lines[8] = `${BORDER_FG}│${RESET}${modalBg}║   No more moves available   ║${RESET}${BORDER_FG}│${RESET}`;
      lines[9] = `${BORDER_FG}│${RESET}${modalBg}║                             ║${RESET}${BORDER_FG}│${RESET}`;
      lines[10] = `${BORDER_FG}│${RESET}${modalBg}║  [U] Undo Move  [R] Restart ║${RESET}${BORDER_FG}│${RESET}`;
      lines[11] = `${BORDER_FG}│${RESET}${modalBg}╚═════════════════════════════╝${RESET}${BORDER_FG}│${RESET}`;
    }

    return lines;
  }

  // Builds the 17-row side panel matching the board height (32 visible columns).
  function buildSidePanelLines() {
    const maxTile = getMaxTile();
    const deltaStr = lastDelta > 0 ? `+${lastDelta}` : "";
    const scoreVal = padLeft(score, 8);
    const bestVal = padLeft(bestScore, 9);
    const movesVal = padLeft(moves, 8);
    const maxVal = padLeft(maxTile, 8);
    const deltaPad = padRight(deltaStr, 7);
    const undoCount = padLeft(history.length, 4);

    return [
      `${PANEL_BORDER_FG}╭──────────────────────────────╮${RESET}`,
      `${PANEL_BORDER_FG}│${RESET} ${LABEL_FG}SCORE${RESET}   ${BOLD}${VALUE_FG}${scoreVal}${RESET} ${BOLD}${DELTA_FG}${deltaPad}${RESET}     ${PANEL_BORDER_FG}│${RESET}`,
      `${PANEL_BORDER_FG}│${RESET} ${LABEL_FG}BEST${RESET}   ${BOLD}${ACCENT_FG}${bestVal}${RESET}             ${PANEL_BORDER_FG}│${RESET}`,
      `${PANEL_BORDER_FG}├──────────────────────────────┤${RESET}`,
      `${PANEL_BORDER_FG}│${RESET} ${LABEL_FG}MOVES${RESET}   ${BOLD}${VALUE_FG}${movesVal}${RESET}  ${MUTED_FG}UNDO:${undoCount}${RESET}  ${PANEL_BORDER_FG}│${RESET}`,
      `${PANEL_BORDER_FG}│${RESET} ${LABEL_FG}MAX TILE${RESET}${BOLD}${ACCENT_FG}${maxVal}${RESET}             ${PANEL_BORDER_FG}│${RESET}`,
      `${PANEL_BORDER_FG}├──────────────────────────────┤${RESET}`,
      `${PANEL_BORDER_FG}│${RESET} ${BOLD}${LABEL_FG}CONTROLS${RESET}                     ${PANEL_BORDER_FG}│${RESET}`,
      `${PANEL_BORDER_FG}│${RESET}  ${KEY_FG}Arrows / WASD${RESET}  Slide Tiles  ${PANEL_BORDER_FG}│${RESET}`,
      `${PANEL_BORDER_FG}│${RESET}  ${KEY_FG}H J K L${RESET}        Vim Slide    ${PANEL_BORDER_FG}│${RESET}`,
      `${PANEL_BORDER_FG}│${RESET}  ${KEY_FG}U / Backspace${RESET}  Undo Move    ${PANEL_BORDER_FG}│${RESET}`,
      `${PANEL_BORDER_FG}│${RESET}  ${KEY_FG}R${RESET}              Restart Game ${PANEL_BORDER_FG}│${RESET}`,
      `${PANEL_BORDER_FG}│${RESET}  ${KEY_FG}Q / Esc${RESET}        Quit         ${PANEL_BORDER_FG}│${RESET}`,
      `${PANEL_BORDER_FG}├──────────────────────────────┤${RESET}`,
      `${PANEL_BORDER_FG}│${RESET} ${MUTED_FG}* marks new / merged tiles${RESET}   ${PANEL_BORDER_FG}│${RESET}`,
      `${PANEL_BORDER_FG}│${RESET} ${MUTED_FG}Combine tiles to reach 2048!${RESET} ${PANEL_BORDER_FG}│${RESET}`,
      `${PANEL_BORDER_FG}╰──────────────────────────────╯${RESET}`,
    ];
  }

  function renderFrame() {
    const { cols, rows } = getTerminalSize();
    const totalContentWidth = 68; // 33 (board) + 3 (gap) + 32 (side panel)
    const totalContentHeight = 21; // 2 header + 17 board/panel + 2 footer
    const leftCol = Math.max(1, Math.floor((cols - totalContentWidth) / 2) + 1);
    const topRow = Math.max(1, Math.floor((rows - totalContentHeight) / 2) + 1);

    const nowMs =
      typeof sys !== "undefined" && typeof sys.uptime === "function"
        ? sys.uptime()
        : Date.now();
    const elapsedSec = Math.max(0, Math.floor((nowMs - startUptimeMs) / 1000));
    const mins = String(Math.floor(elapsedSec / 60)).padStart(2, "0");
    const secs = String(elapsedSec % 60).padStart(2, "0");

    const boardLines = buildBoardLines();
    const panelLines = buildSidePanelLines();

    let out = SYNC_START + "\x1b[2J";

    // Row 0 of 21: Header bar
    const badgeStyle = bgRgb(237, 194, 46) + fgRgb(40, 36, 30) + BOLD;
    out += `\x1b[${topRow};${leftCol}H`;
    out += `${badgeStyle}  2 0 4 8  ${RESET}  ${BOLD}${VALUE_FG}Sliding Tile Puzzle${RESET}                    ${MUTED_FG}Time ${mins}:${secs}${RESET}`;

    // Row 1 of 21: Blank spacer row
    out += `\x1b[${topRow + 1};${leftCol}H`;

    // Rows 2..18 of 21: 17-row Board + Side Panel
    for (let i = 0; i < 17; ++i) {
      out += `\x1b[${topRow + 2 + i};${leftCol}H${boardLines[i]}   ${panelLines[i]}`;
    }

    // Row 19 of 21: Blank spacer row
    out += `\x1b[${topRow + 19};${leftCol}H`;

    // Row 20 of 21: Status message line
    out += `\x1b[${topRow + 20};${leftCol}H`;
    if (hasWon && !endlessMode) {
      out += `${BOLD}${ACCENT_FG}★ Congratulations! Press [C] to continue in endless mode or [R] to restart.${RESET}`;
    } else if (gameOver) {
      out += `${BOLD}${fgRgb(243, 139, 168)}Game Over! Press [U] to undo your last move or [R] to start a new game.${RESET}`;
    } else if (endlessMode) {
      out += `${ACCENT_FG}★ Endless Mode active — keep merging for a higher tile!${RESET}`;
    } else {
      out += `${MUTED_FG}Slide tiles with Arrow keys, WASD, or HJKL. Press U to undo, Q to quit.${RESET}`;
    }

    out += SYNC_END;
    term.write(out);
  }

  function decodeAction(rawKey) {
    if (!rawKey) return null;
    const key =
      typeof rawKey === "string"
        ? rawKey
        : rawKey.key || rawKey.name || rawKey.sequence || "";
    const lower = key.toLowerCase();

    if (
      key === "\x1b[A" ||
      key === "\x1bOA" ||
      lower === "up" ||
      lower === "arrowup" ||
      lower === "uparrow" ||
      lower === "w" ||
      lower === "k"
    ) {
      return "up";
    }
    if (
      key === "\x1b[B" ||
      key === "\x1bOB" ||
      lower === "down" ||
      lower === "arrowdown" ||
      lower === "downarrow" ||
      lower === "s" ||
      lower === "j"
    ) {
      return "down";
    }
    if (
      key === "\x1b[D" ||
      key === "\x1bOD" ||
      lower === "left" ||
      lower === "arrowleft" ||
      lower === "leftarrow" ||
      lower === "a" ||
      lower === "h"
    ) {
      return "left";
    }
    if (
      key === "\x1b[C" ||
      key === "\x1bOC" ||
      lower === "right" ||
      lower === "arrowright" ||
      lower === "rightarrow" ||
      lower === "d" ||
      lower === "l"
    ) {
      return "right";
    }
    if (
      lower === "u" ||
      lower === "backspace" ||
      key === "\x7f" ||
      key === "\b" ||
      key === "\x08"
    ) {
      return "undo";
    }
    if (lower === "r") {
      return "restart";
    }
    if (lower === "c" || lower === "enter" || key === "\r" || key === "\n") {
      return "continue";
    }
    if (
      lower === "q" ||
      lower === "escape" ||
      lower === "esc" ||
      key === "\x1b" ||
      lower === "ctrl+c" ||
      lower === "ctrl-c" ||
      key === "\x03" ||
      key === "\x04"
    ) {
      return "quit";
    }
    return null;
  }

  try {
    term.altScreen(true);
    term.rawMode(true);
    term.cursor(false);
    term.title("2048 — jsshell");

    resetGame();
    renderFrame();

    while (true) {
      const key = await term.readKey();
      if (key === null || key === undefined || key === "") continue;

      const action = decodeAction(key);
      if (action === "quit") {
        break;
      } else if (action === "restart") {
        resetGame();
        renderFrame();
      } else if (action === "undo") {
        if (undoMove()) renderFrame();
      } else if (action === "continue") {
        if (hasWon && !endlessMode) {
          endlessMode = true;
          renderFrame();
        }
      } else if (
        action === "up" ||
        action === "down" ||
        action === "left" ||
        action === "right"
      ) {
        if (slide(action)) renderFrame();
      }
    }
  } finally {
    term.cursor(true);
    term.rawMode(false);
    term.altScreen(false);
  }
})();
