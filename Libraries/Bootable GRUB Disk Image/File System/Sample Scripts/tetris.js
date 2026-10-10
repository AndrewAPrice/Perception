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

// Tetrominoes — Playable falling-block arcade game for jsshell.
// Run with: jsshell "/Sample Scripts/tetris.js"

const COLS = 10;
const ROWS = 20;
const LOCK_DELAY_MS = 500;
const MAX_LOCK_MOVES = 15;
const CLEAR_FLASH_MS = 180;

const ESC = "\x1b";
const CSI = "\x1b[";
const SYNC_START = "\x1b[?2026h";
const SYNC_END = "\x1b[?2026l";
const RESET = "\x1b[0m";

function fg(r, g, b) {
  return `\x1b[38;2;${r};${g};${b}m`;
}

function bg(r, g, b) {
  return `\x1b[48;2;${r};${g};${b}m`;
}

function moveTo(row, col) {
  return `\x1b[${row};${col}H`;
}

const COLOR_BG = bg(14, 16, 24);
const COLOR_PANEL_BG = bg(20, 24, 36);
const COLOR_WELL_BG = bg(10, 12, 18);
const COLOR_BORDER = fg(100, 116, 155);
const COLOR_WELL_BORDER = fg(140, 165, 220);
const COLOR_TITLE = fg(120, 210, 255) + "\x1b[1m";
const COLOR_LABEL = fg(150, 165, 195);
const COLOR_VALUE = fg(245, 248, 255) + "\x1b[1m";
const COLOR_KEY = fg(255, 215, 100) + "\x1b[1m";
const COLOR_DIM = fg(90, 100, 125);
const COLOR_GRID_DOT = fg(28, 33, 48);
const COLOR_FLASH = fg(255, 255, 255) + bg(220, 235, 255) + "\x1b[1m";

// 7 standard Tetromino definitions with 4 SRS rotation states (0, R, 2, L),
// 24-bit RGB bevel colors, and preview centering offsets.
const PIECES = {
  I: {
    name: "I",
    fg: [0, 240, 240],
    hi: [140, 255, 255],
    ghost: [0, 130, 140],
    states: [
      [[0, 1], [1, 1], [2, 1], [3, 1]],
      [[2, 0], [2, 1], [2, 2], [2, 3]],
      [[0, 2], [1, 2], [2, 2], [3, 2]],
      [[1, 0], [1, 1], [1, 2], [1, 3]],
    ],
  },
  O: {
    name: "O",
    fg: [245, 215, 35],
    hi: [255, 245, 140],
    ghost: [140, 125, 25],
    states: [
      [[1, 0], [2, 0], [1, 1], [2, 1]],
      [[1, 0], [2, 0], [1, 1], [2, 1]],
      [[1, 0], [2, 0], [1, 1], [2, 1]],
      [[1, 0], [2, 0], [1, 1], [2, 1]],
    ],
  },
  T: {
    name: "T",
    fg: [175, 65, 245],
    hi: [220, 150, 255],
    ghost: [105, 40, 145],
    states: [
      [[1, 0], [0, 1], [1, 1], [2, 1]],
      [[1, 0], [1, 1], [2, 1], [1, 2]],
      [[0, 1], [1, 1], [2, 1], [1, 2]],
      [[1, 0], [0, 1], [1, 1], [1, 2]],
    ],
  },
  S: {
    name: "S",
    fg: [55, 225, 95],
    hi: [150, 255, 175],
    ghost: [35, 130, 55],
    states: [
      [[1, 0], [2, 0], [0, 1], [1, 1]],
      [[1, 0], [1, 1], [2, 1], [2, 2]],
      [[1, 1], [2, 1], [0, 2], [1, 2]],
      [[0, 0], [0, 1], [1, 1], [1, 2]],
    ],
  },
  Z: {
    name: "Z",
    fg: [245, 60, 75],
    hi: [255, 150, 160],
    ghost: [145, 35, 45],
    states: [
      [[0, 0], [1, 0], [1, 1], [2, 1]],
      [[2, 0], [1, 1], [2, 1], [1, 2]],
      [[0, 1], [1, 1], [1, 2], [2, 2]],
      [[1, 0], [0, 1], [1, 1], [0, 2]],
    ],
  },
  J: {
    name: "J",
    fg: [55, 115, 250],
    hi: [145, 185, 255],
    ghost: [35, 70, 150],
    states: [
      [[0, 0], [0, 1], [1, 1], [2, 1]],
      [[1, 0], [2, 0], [1, 1], [1, 2]],
      [[0, 1], [1, 1], [2, 1], [2, 2]],
      [[1, 0], [1, 1], [0, 2], [1, 2]],
    ],
  },
  L: {
    name: "L",
    fg: [250, 145, 40],
    hi: [255, 205, 135],
    ghost: [150, 85, 25],
    states: [
      [[2, 0], [0, 1], [1, 1], [2, 1]],
      [[1, 0], [1, 1], [1, 2], [2, 2]],
      [[0, 1], [1, 1], [2, 1], [0, 2]],
      [[0, 0], [1, 0], [1, 1], [1, 2]],
    ],
  },
};

const PIECE_KEYS = ["I", "O", "T", "S", "Z", "J", "L"];

// Precompute styled 2-char block strings for each piece and ghost.
const BLOCK_STR = {};
const GHOST_STR = {};
for (const k of PIECE_KEYS) {
  const p = PIECES[k];
  BLOCK_STR[k] =
    fg(p.hi[0], p.hi[1], p.hi[2]) +
    bg(p.fg[0], p.fg[1], p.fg[2]) +
    "▓█" +
    RESET;
  GHOST_STR[k] =
    fg(p.ghost[0], p.ghost[1], p.ghost[2]) +
    COLOR_WELL_BG +
    "░░" +
    RESET;
}

// SRS Wall Kick tables (dx, dy where +y is downward on screen).
// Standard SRS tables have +y upward, so dy values here are inverted for screen coordinates.
const JLSTZ_KICKS = {
  "0>1": [[0, 0], [-1, 0], [-1, -1], [0, 2], [-1, 2]],
  "1>0": [[0, 0], [1, 0], [1, 1], [0, -2], [1, -2]],
  "1>2": [[0, 0], [1, 0], [1, 1], [0, -2], [1, -2]],
  "2>1": [[0, 0], [-1, 0], [-1, -1], [0, 2], [-1, 2]],
  "2>3": [[0, 0], [1, 0], [1, -1], [0, 2], [1, 2]],
  "3>2": [[0, 0], [-1, 0], [-1, 1], [0, -2], [-1, -2]],
  "3>0": [[0, 0], [-1, 0], [-1, 1], [0, -2], [-1, -2]],
  "0>3": [[0, 0], [1, 0], [1, -1], [0, 2], [1, 2]],
};

const I_KICKS = {
  "0>1": [[0, 0], [-2, 0], [1, 0], [-2, 1], [1, -2]],
  "1>0": [[0, 0], [2, 0], [-1, 0], [2, -1], [-1, 2]],
  "1>2": [[0, 0], [-1, 0], [2, 0], [-1, -2], [2, 1]],
  "2>1": [[0, 0], [1, 0], [-2, 0], [1, 2], [-2, -1]],
  "2>3": [[0, 0], [2, 0], [-1, 0], [2, -1], [-1, 2]],
  "3>2": [[0, 0], [-2, 0], [1, 0], [-2, 1], [1, -2]],
  "3>0": [[0, 0], [1, 0], [-2, 0], [1, 2], [-2, -1]],
  "0>3": [[0, 0], [-1, 0], [2, 0], [-1, -2], [2, 1]],
};

function nowMs() {
  return Math.floor(sys.uptime());
}

function gravityDelayMs(level) {
  // Smooth arcade speed curve from 800ms at level 1 down to 55ms at level 15+.
  const delay = Math.floor(800 * Math.pow(0.82, level - 1));
  return Math.max(45, delay);
}

function padLeft(val, width) {
  const s = String(val);
  return s.length >= width ? s : " ".repeat(width - s.length) + s;
}

function padRight(val, width) {
  const s = String(val);
  return s.length >= width ? s.slice(0, width) : s + " ".repeat(width - s.length);
}

class TetrisGame {
  constructor() {
    this.highScore = 0;
    this.reset();
  }

  reset() {
    this.board = [];
    for (let r = 0; r < ROWS; r++) {
      this.board.push(new Array(COLS).fill(null));
    }
    this.bag = [];
    this.nextQueue = [];
    for (let i = 0; i < 4; i++) {
      this.nextQueue.push(this.drawFromBag());
    }
    this.holdPiece = null;
    this.canHold = true;

    this.score = 0;
    this.lines = 0;
    this.level = 1;
    this.combo = -1;
    this.lastActionText = "";
    this.lastActionTime = 0;

    this.paused = false;
    this.gameOver = false;
    this.clearingRows = [];
    this.clearEndTime = 0;

    this.active = null;
    this.lastGravityTime = nowMs();
    this.lockStartTime = null;
    this.lockMoves = 0;

    this.spawnNext();
  }

  drawFromBag() {
    if (this.bag.length === 0) {
      this.bag = PIECE_KEYS.slice();
      for (let i = this.bag.length - 1; i > 0; i--) {
        const j = Math.floor(Math.random() * (i + 1));
        const tmp = this.bag[i];
        this.bag[i] = this.bag[j];
        this.bag[j] = tmp;
      }
    }
    return this.bag.pop();
  }

  spawnPiece(type) {
    this.active = {
      type: type,
      rot: 0,
      x: 3,
      y: 0,
    };
    this.lockStartTime = null;
    this.lockMoves = 0;
    this.lastGravityTime = nowMs();

    if (!this.isValid(this.active.x, this.active.y, this.active.rot)) {
      // Try spawning 1 row higher if top row is crowded.
      if (this.isValid(this.active.x, -1, this.active.rot)) {
        this.active.y = -1;
      } else {
        this.gameOver = true;
      }
    }
    this.updateLockState();
  }

  spawnNext() {
    const nextType = this.nextQueue.shift();
    this.nextQueue.push(this.drawFromBag());
    this.canHold = true;
    this.spawnPiece(nextType);
  }

  getCells(x, y, rot, type = this.active.type) {
    const offsets = PIECES[type].states[rot];
    const res = [];
    for (let i = 0; i < 4; i++) {
      res.push([x + offsets[i][0], y + offsets[i][1]]);
    }
    return res;
  }

  isValid(x, y, rot, type = this.active.type) {
    const cells = this.getCells(x, y, rot, type);
    for (let i = 0; i < 4; i++) {
      const cx = cells[i][0];
      const cy = cells[i][1];
      if (cx < 0 || cx >= COLS || cy >= ROWS) return false;
      if (cy >= 0 && this.board[cy][cx] !== null) return false;
    }
    return true;
  }

  isGrounded() {
    if (!this.active) return false;
    return !this.isValid(this.active.x, this.active.y + 1, this.active.rot);
  }

  updateLockState() {
    if (!this.active) return;
    if (this.isGrounded()) {
      if (this.lockStartTime === null) {
        this.lockStartTime = nowMs();
      }
    } else {
      this.lockStartTime = null;
    }
  }

  resetLockDelayOnMove() {
    if (this.lockStartTime !== null && this.lockMoves < MAX_LOCK_MOVES) {
      this.lockStartTime = nowMs();
      this.lockMoves++;
    }
    this.updateLockState();
  }

  moveHorizontal(dx) {
    if (!this.active || this.paused || this.gameOver || this.clearingRows.length > 0) return false;
    if (this.isValid(this.active.x + dx, this.active.y, this.active.rot)) {
      this.active.x += dx;
      this.resetLockDelayOnMove();
      return true;
    }
    return false;
  }

  rotate(dir) {
    if (!this.active || this.paused || this.gameOver || this.clearingRows.length > 0) return false;
    const oldRot = this.active.rot;
    const newRot = (oldRot + dir + 4) % 4;
    const type = this.active.type;

    if (type === "O") {
      this.active.rot = newRot;
      return true;
    }

    const key = `${oldRot}>${newRot}`;
    const kicks = type === "I" ? I_KICKS[key] : JLSTZ_KICKS[key];
    for (let i = 0; i < kicks.length; i++) {
      const kx = kicks[i][0];
      const ky = kicks[i][1];
      if (this.isValid(this.active.x + kx, this.active.y + ky, newRot)) {
        this.active.x += kx;
        this.active.y += ky;
        this.active.rot = newRot;
        this.resetLockDelayOnMove();
        return true;
      }
    }
    return false;
  }

  softDrop() {
    if (!this.active || this.paused || this.gameOver || this.clearingRows.length > 0) return false;
    if (this.isValid(this.active.x, this.active.y + 1, this.active.rot)) {
      this.active.y++;
      this.score += 1;
      if (this.score > this.highScore) this.highScore = this.score;
      this.lastGravityTime = nowMs();
      this.updateLockState();
      return true;
    }
    return false;
  }

  hardDrop() {
    if (!this.active || this.paused || this.gameOver || this.clearingRows.length > 0) return;
    let dropped = 0;
    while (this.isValid(this.active.x, this.active.y + 1, this.active.rot)) {
      this.active.y++;
      dropped++;
    }
    this.score += dropped * 2;
    if (this.score > this.highScore) this.highScore = this.score;
    this.lockPiece();
  }

  hold() {
    if (!this.active || !this.canHold || this.paused || this.gameOver || this.clearingRows.length > 0) {
      return;
    }
    const currentType = this.active.type;
    const prevHold = this.holdPiece;
    this.holdPiece = currentType;
    this.canHold = false;
    if (prevHold === null) {
      this.spawnNext();
    } else {
      this.spawnPiece(prevHold);
    }
  }

  getGhostY() {
    if (!this.active) return 0;
    let gy = this.active.y;
    while (this.isValid(this.active.x, gy + 1, this.active.rot)) {
      gy++;
    }
    return gy;
  }

  lockPiece() {
    if (!this.active) return;
    const cells = this.getCells(this.active.x, this.active.y, this.active.rot);
    let aboveTop = true;
    for (let i = 0; i < 4; i++) {
      const cx = cells[i][0];
      const cy = cells[i][1];
      if (cy >= 0 && cy < ROWS) {
        this.board[cy][cx] = this.active.type;
        aboveTop = false;
      }
    }
    this.active = null;
    this.lockStartTime = null;

    if (aboveTop) {
      this.gameOver = true;
      return;
    }

    // Check for completed lines.
    const fullRows = [];
    for (let r = 0; r < ROWS; r++) {
      let full = true;
      for (let c = 0; c < COLS; c++) {
        if (this.board[r][c] === null) {
          full = false;
          break;
        }
      }
      if (full) fullRows.push(r);
    }

    if (fullRows.length > 0) {
      this.clearingRows = fullRows;
      this.clearEndTime = nowMs() + CLEAR_FLASH_MS;

      const count = fullRows.length;
      const basePoints = [0, 100, 300, 500, 800][count] || 800;
      this.combo++;
      const comboBonus = this.combo > 0 ? 50 * this.combo * this.level : 0;
      this.score += basePoints * this.level + comboBonus;
      this.lines += count;
      this.level = Math.floor(this.lines / 10) + 1;
      if (this.score > this.highScore) this.highScore = this.score;

      const names = ["", "SINGLE!", "DOUBLE!", "TRIPLE!", "TETRIS!!"];
      this.lastActionText = names[count] || "CLEAR!";
      this.lastActionTime = nowMs();
    } else {
      this.combo = -1;
      this.spawnNext();
    }
  }

  finishLineClear() {
    const newBoard = [];
    for (let r = 0; r < ROWS; r++) {
      if (!this.clearingRows.includes(r)) {
        newBoard.push(this.board[r]);
      }
    }
    while (newBoard.length < ROWS) {
      newBoard.unshift(new Array(COLS).fill(null));
    }
    this.board = newBoard;
    this.clearingRows = [];
    this.spawnNext();
  }

  step() {
    if (this.paused || this.gameOver) return;
    const now = nowMs();

    if (this.clearingRows.length > 0) {
      if (now >= this.clearEndTime) {
        this.finishLineClear();
      }
      return;
    }

    if (!this.active) return;

    const delay = gravityDelayMs(this.level);
    while (now - this.lastGravityTime >= delay) {
      this.lastGravityTime += delay;
      if (this.isValid(this.active.x, this.active.y + 1, this.active.rot)) {
        this.active.y++;
      } else {
        break;
      }
    }

    this.updateLockState();
    if (this.lockStartTime !== null && now - this.lockStartTime >= LOCK_DELAY_MS) {
      this.lockPiece();
    }
  }

  nextTickSliceMs() {
    if (this.paused || this.gameOver) return 100;
    const now = nowMs();
    if (this.clearingRows.length > 0) {
      return Math.max(5, Math.min(50, this.clearEndTime - now));
    }
    const delay = gravityDelayMs(this.level);
    const untilGravity = Math.max(5, delay - (now - this.lastGravityTime));
    if (this.lockStartTime !== null) {
      const untilLock = Math.max(5, LOCK_DELAY_MS - (now - this.lockStartTime));
      return Math.min(35, untilGravity, untilLock);
    }
    return Math.min(35, untilGravity);
  }
}

function drawBox(top, left, width, height, title) {
  let out = "";
  const inner = width - 2;
  let topBar = "┌" + "─".repeat(inner) + "┐";
  if (title) {
    const label = ` ${title} `;
    const start = Math.max(1, Math.floor((width - label.length) / 2));
    topBar =
      "┌" +
      "─".repeat(start - 1) +
      COLOR_TITLE +
      label +
      RESET +
      COLOR_PANEL_BG +
      COLOR_BORDER +
      "─".repeat(Math.max(0, width - 1 - start - label.length)) +
      "┐";
  }
  out += moveTo(top, left) + COLOR_PANEL_BG + COLOR_BORDER + topBar;
  for (let r = 1; r < height - 1; r++) {
    out +=
      moveTo(top + r, left) +
      COLOR_PANEL_BG +
      COLOR_BORDER +
      "│" +
      " ".repeat(inner) +
      "│";
  }
  out +=
    moveTo(top + height - 1, left) +
    COLOR_PANEL_BG +
    COLOR_BORDER +
    "└" +
    "─".repeat(inner) +
    "┘" +
    RESET;
  return out;
}

function renderMiniPiece(type, top, left, dimmed = false) {
  let out = "";
  for (let r = 0; r < 2; r++) {
    out += moveTo(top + r, left) + COLOR_PANEL_BG + "        ";
  }
  if (!type) return out;
  const cells = PIECES[type].states[0];
  const block = dimmed
    ? COLOR_DIM + COLOR_PANEL_BG + "▒▒" + RESET
    : BLOCK_STR[type];
  const xOffset = type === "O" ? 0 : type === "I" ? 0 : 1;
  for (let i = 0; i < 4; i++) {
    const cx = cells[i][0];
    const cy = cells[i][1];
    if (cy >= 0 && cy < 2) {
      out += moveTo(top + cy, left + xOffset + cx * 2) + block;
    }
  }
  return out;
}

function renderFrame(game, cols, rows) {
  const layoutWidth = 64;
  const layoutHeight = 22;
  const startCol = Math.max(2, Math.floor((cols - layoutWidth) / 2) + 1);
  const startRow = Math.max(1, Math.floor((rows - layoutHeight) / 2) + 1);

  const leftCol = startCol;
  const wellCol = startCol + 17;
  const rightCol = wellCol + 23;

  let out = SYNC_START + RESET + COLOR_BG;

  // Clear background canvas cleanly.
  const blankLine = " ".repeat(Math.max(1, cols));
  for (let r = 1; r <= rows; r++) {
    out += moveTo(r, 1) + COLOR_BG + blankLine;
  }

  // Left Column: HOLD box & STATS box.
  out += drawBox(startRow, leftCol, 15, 6, "HOLD [C]");
  out += renderMiniPiece(game.holdPiece, startRow + 2, leftCol + 3, !game.canHold);

  out += drawBox(startRow + 6, leftCol, 15, 16, "STATS");
  const stats = [
    ["SCORE", padLeft(game.score, 9)],
    ["HIGH", padLeft(game.highScore, 9)],
    ["LINES", padLeft(game.lines, 9)],
    ["LEVEL", padLeft(game.level, 9)],
    ["COMBO", padLeft(game.combo > 0 ? `${game.combo}x` : "-", 9)],
  ];
  let sr = startRow + 8;
  for (let i = 0; i < stats.length; i++) {
    out +=
      moveTo(sr, leftCol + 2) +
      COLOR_PANEL_BG +
      COLOR_LABEL +
      padRight(stats[i][0], 11);
    out +=
      moveTo(sr + 1, leftCol + 2) +
      COLOR_PANEL_BG +
      COLOR_VALUE +
       padLeft(stats[i][1], 11) +
      RESET;
    sr += 3;
  }

  // Recent clear action banner inside left panel if active.
  if (game.lastActionText && nowMs() - game.lastActionTime < 1400) {
    out +=
      moveTo(startRow + 20, leftCol + 2) +
      COLOR_PANEL_BG +
      COLOR_KEY +
      padRight(game.lastActionText, 11) +
      RESET;
  }

  // Center Column: 10x20 Playfield Well (20 chars wide inside border).
  out +=
    moveTo(startRow, wellCol) +
    COLOR_WELL_BG +
    COLOR_WELL_BORDER +
    "╔════ TETROMINOES ═══╗";
  for (let r = 0; r < ROWS; r++) {
    out +=
      moveTo(startRow + 1 + r, wellCol) +
      COLOR_WELL_BG +
      COLOR_WELL_BORDER +
      "║" +
      " ".repeat(COLS * 2) +
      "║";
  }
  out +=
    moveTo(startRow + ROWS + 1, wellCol) +
    COLOR_WELL_BG +
    COLOR_WELL_BORDER +
    "╚" +
    "═".repeat(COLS * 2) +
    "╝" +
    RESET;

  // Build active and ghost cell lookup maps for fast playfield rendering.
  const ghostMap = {};
  const activeMap = {};
  if (game.active && game.clearingRows.length === 0) {
    const gy = game.getGhostY();
    const gCells = game.getCells(game.active.x, gy, game.active.rot);
    for (let i = 0; i < 4; i++) {
      ghostMap[`${gCells[i][0]},${gCells[i][1]}`] = game.active.type;
    }
    const aCells = game.getCells(game.active.x, game.active.y, game.active.rot);
    for (let i = 0; i < 4; i++) {
      activeMap[`${aCells[i][0]},${aCells[i][1]}`] = game.active.type;
    }
  }

  // Render playfield cells row by row.
  for (let r = 0; r < ROWS; r++) {
    let rowStr = moveTo(startRow + 1 + r, wellCol + 1);
    if (game.clearingRows.includes(r)) {
      rowStr += COLOR_FLASH + "████████████████████" + RESET;
      out += rowStr;
      continue;
    }
    for (let c = 0; c < COLS; c++) {
      const key = `${c},${r}`;
      const locked = game.board[r][c];
      if (activeMap[key]) {
        rowStr += BLOCK_STR[activeMap[key]];
      } else if (locked) {
        rowStr += BLOCK_STR[locked];
      } else if (ghostMap[key]) {
        rowStr += GHOST_STR[ghostMap[key]];
      } else {
        rowStr += COLOR_WELL_BG + COLOR_GRID_DOT + " ·" + RESET;
      }
    }
    out += rowStr;
  }

  // Overlay for PAUSED or GAME OVER states.
  if (game.paused || game.gameOver) {
    const boxTop = startRow + 8;
    const boxLeft = wellCol + 2;
    const title = game.gameOver ? "  GAME OVER!  " : "    PAUSED    ";
    const sub1 = game.gameOver ? " [R] Play Again " : " [P] Resume     ";
    const sub2 = " [Q] Quit       ";
    out +=
      moveTo(boxTop, boxLeft) +
      COLOR_PANEL_BG +
      COLOR_WELL_BORDER +
      "╔════════════════╗" +
      moveTo(boxTop + 1, boxLeft) +
      "║" +
      COLOR_KEY +
       padRight(title, 16) +
      RESET +
      COLOR_PANEL_BG +
      COLOR_WELL_BORDER +
      "║" +
      moveTo(boxTop + 2, boxLeft) +
      "║                ║" +
      moveTo(boxTop + 3, boxLeft) +
      "║" +
      COLOR_VALUE +
      padRight(sub1, 16) +
      RESET +
      COLOR_PANEL_BG +
      COLOR_WELL_BORDER +
      "║" +
      moveTo(boxTop + 4, boxLeft) +
      "║" +
      COLOR_LABEL +
      padRight(sub2, 16) +
      RESET +
      COLOR_PANEL_BG +
      COLOR_WELL_BORDER +
      "║" +
      moveTo(boxTop + 5, boxLeft) +
      "╚════════════════╝" +
      RESET;
  }

  // Right Column: NEXT 3 pieces box & CONTROLS legend box.
  out += drawBox(startRow, rightCol, 22, 10, "NEXT");
  for (let i = 0; i < 3; i++) {
    out += renderMiniPiece(game.nextQueue[i], startRow + 2 + i * 2 + (i > 0 ? i : 0), rightCol + 7, false);
  }

  out += drawBox(startRow + 10, rightCol, 22, 12, "CONTROLS");
  const controls = [
    ["←/→ A/D", "Move"],
    ["↑/W/X", "Rotate CW"],
    ["Z", "Rotate CCW"],
    ["↓/S", "Soft Drop"],
    ["Space", "Hard Drop"],
    ["C", "Hold Piece"],
    ["P", "Pause"],
    ["R", "Restart"],
    ["Q / Esc", "Quit"],
  ];
  for (let i = 0; i < controls.length; i++) {
    out +=
      moveTo(startRow + 11 + i, rightCol + 2) +
      COLOR_PANEL_BG +
      COLOR_KEY +
      padRight(controls[i][0], 8) +
      RESET +
      COLOR_PANEL_BG +
      COLOR_LABEL +
      padRight(controls[i][1], 10) +
      RESET;
  }

  out += SYNC_END;
  return out;
}

async function main() {
  const game = new TetrisGame();
  term.altScreen(true);
  term.rawMode(true);
  term.cursor(false);
  term.title("Tetrominoes — jsshell");

  try {
    let running = true;
    while (running) {
      game.step();

      const size = term.size();
      const cols = size && size.cols ? size.cols : 80;
      const rows = size && size.rows ? size.rows : 24;
      term.write(renderFrame(game, cols, rows));

      const waitMs = game.nextTickSliceMs();
      const key = await term.readKey(waitMs);
      if (!key) continue;

    // Support both canonical key names and raw ANSI escape sequences.
    const k =
      key === "\x1b[D" || key === "Left"
        ? "ArrowLeft"
        : key === "\x1b[C" || key === "Right"
        ? "ArrowRight"
        : key === "\x1b[A" || key === "Up"
        ? "ArrowUp"
        : key === "\x1b[B" || key === "Down"
        ? "ArrowDown"
        : key === "\x1b"
        ? "Escape"
        : key === "\x03"
        ? "Ctrl+C"
        : key === "Space"
        ? " "
        : key;

      if (k === "q" || k === "Q" || k === "Escape" || k === "Ctrl+C") {
        running = false;
        break;
      }

      if (k === "r" || k === "R") {
        game.reset();
        continue;
      }

      if (k === "p" || k === "P") {
        if (!game.gameOver) {
          game.paused = !game.paused;
          game.lastGravityTime = nowMs();
          if (game.lockStartTime !== null) {
            game.lockStartTime = nowMs();
          }
        }
        continue;
      }

      if (game.gameOver) {
        if (k === "Enter" || k === "\r" || k === "\n" || k === " ") {
          game.reset();
        }
        continue;
      }

      if (game.paused) continue;

      switch (k) {
        case "ArrowLeft":
        case "a":
        case "A":
          game.moveHorizontal(-1);
          break;
        case "ArrowRight":
        case "d":
        case "D":
          game.moveHorizontal(1);
          break;
        case "ArrowUp":
        case "w":
        case "W":
        case "x":
        case "X":
          game.rotate(1);
          break;
        case "z":
        case "Z":
          game.rotate(-1);
          break;
        case "ArrowDown":
        case "s":
        case "S":
          game.softDrop();
          break;
        case " ":
          game.hardDrop();
          break;
        case "c":
        case "C":
          game.hold();
          break;
      }
    }
  } finally {
    term.cursor(true);
    term.rawMode(false);
    term.altScreen(false);
  }
}

await main();
