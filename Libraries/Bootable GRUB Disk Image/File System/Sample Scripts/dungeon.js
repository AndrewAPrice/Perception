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

// ============================================================================
// Dungeon of Perception — Procedural Roguelike Dungeon Crawler for jsshell
// Run from jsshell:
//   /run "/Sample Scripts/dungeon.js"
// ============================================================================

(() => {
  // 24-bit ANSI TrueColor helpers & synchronized output sequences.
  const ESC = "\x1b";
  const SYNC_START = `${ESC}[?2026h`;
  const SYNC_END = `${ESC}[?2026l`;
  const HOME = `${ESC}[H`;
  const RESET = `${ESC}[0m`;
  const BOLD = `${ESC}[1m`;

  function fg(r, g, b) {
    return `${ESC}[38;2;${r};${g};${b}m`;
  }

  function bg(r, g, b) {
    return `${ESC}[48;2;${r};${g};${b}m`;
  }

  const COLORS = {
    frame: fg(95, 115, 155),
    frameTitle: BOLD + fg(130, 200, 255),
    sidebarLabel: fg(150, 165, 190),
    sidebarValue: BOLD + fg(235, 240, 250),
    fog: fg(52, 65, 92),
    wall: fg(118, 132, 160),
    floor: fg(75, 90, 115),
    door: BOLD + fg(210, 155, 80),
    stairs: BOLD + fg(110, 230, 255),
    shrine: BOLD + fg(90, 250, 190),
    player: BOLD + fg(255, 240, 90),
    amulet: BOLD + fg(255, 215, 0),
    gold: BOLD + fg(255, 210, 60),
    potion: BOLD + fg(255, 95, 135),
    scrollFire: BOLD + fg(255, 145, 55),
    scrollTele: BOLD + fg(165, 130, 255),
    weapon: BOLD + fg(125, 215, 255),
    armor: BOLD + fg(150, 235, 165),
    hpHigh: fg(80, 235, 125),
    hpMid: fg(250, 200, 65),
    hpLow: fg(255, 85, 85),
    hpEmpty: fg(55, 62, 78),
    logInfo: fg(190, 205, 230),
    logCombat: fg(255, 185, 110),
    logDanger: BOLD + fg(255, 95, 95),
    logLoot: BOLD + fg(120, 240, 165),
    logMagic: BOLD + fg(185, 145, 255),
    logVictory: BOLD + fg(255, 225, 75),
  };

  // Viewport and layout constants:
  // Top-left: 56x19 framed map viewport (inner map 54x17)
  // Top-right: 24x19 framed status & inventory sidebar (inner width 22)
  // Bottom: 80x5 framed 3-line combat log + controls legend
  const MAP_BOX_W = 56;
  const MAP_BOX_H = 19;
  const MAP_W = MAP_BOX_W - 2; // 54
  const MAP_H = MAP_BOX_H - 2; // 17
  const SIDE_BOX_W = 24;
  const SIDE_W = SIDE_BOX_W - 2; // 22
  const TOTAL_W = 80;
  const MAX_FLOORS = 5;
  const FOV_RADIUS = 7;

  // Tile types.
  const TILE_WALL = "#";
  const TILE_FLOOR = "·";
  const TILE_DOOR = "+";
  const TILE_STAIRS = ">";
  const TILE_SHRINE = "▲";

  // Equipment progression tiers.
  const WEAPONS = [
    { name: "Rusty Dagger", atk: 2, tier: 0 },
    { name: "Iron Shortsword", atk: 5, tier: 1 },
    { name: "Sunfire Blade", atk: 9, tier: 2 },
    { name: "Void Cleaver", atk: 14, tier: 3 },
  ];

  const ARMORS = [
    { name: "Cloth Tunic", def: 1, tier: 0 },
    { name: "Leather Jerkin", def: 3, tier: 1 },
    { name: "Chainmail", def: 6, tier: 2 },
    { name: "Aegis Plate", def: 10, tier: 3 },
  ];

  // Monster templates with distinct stats and tactical behaviors.
  const MONSTER_DEFS = {
    rat: {
      glyph: "r",
      name: "Giant Rat",
      color: fg(195, 155, 115),
      hp: 8,
      atk: 4,
      def: 0,
      xp: 6,
      minFloor: 1,
      maxFloor: 2,
    },
    goblin: {
      glyph: "g",
      name: "Cave Goblin",
      color: fg(115, 225, 110),
      hp: 13,
      atk: 6,
      def: 1,
      xp: 10,
      minFloor: 1,
      maxFloor: 3,
    },
    archer: {
      glyph: "a",
      name: "Skeleton Archer",
      color: BOLD + fg(235, 235, 220),
      hp: 14,
      atk: 7,
      def: 1,
      xp: 15,
      minFloor: 2,
      maxFloor: 4,
      ranged: true,
    },
    orc: {
      glyph: "o",
      name: "Orc Brute",
      color: BOLD + fg(255, 140, 70),
      hp: 26,
      atk: 10,
      def: 3,
      xp: 22,
      minFloor: 2,
      maxFloor: 5,
      heavyStrike: true,
    },
    wraith: {
      glyph: "w",
      name: "Shadow Wraith",
      color: BOLD + fg(190, 125, 255),
      hp: 22,
      atk: 9,
      def: 2,
      xp: 28,
      minFloor: 3,
      maxFloor: 5,
      phases: true,
      ignoresArmor: true,
    },
    dragon: {
      glyph: "D",
      name: "Elder Dragon",
      color: BOLD + fg(255, 70, 70),
      hp: 75,
      atk: 16,
      def: 5,
      xp: 150,
      minFloor: 5,
      maxFloor: 5,
      boss: true,
    },
  };

  function randInt(min, max) {
    return Math.floor(Math.random() * (max - min + 1)) + min;
  }

  function clamp(val, min, max) {
    return Math.max(min, Math.min(max, val));
  }

  function padRight(str, width) {
    if (str.length >= width) return str.slice(0, width);
    return str + " ".repeat(width - str.length);
  }

  // Normalizes key inputs from `term.readKey()` across VT100 sequences and named keys.
  function normalizeKey(raw) {
    if (!raw) return "";
    const k = typeof raw === "string" ? raw : (raw.key || "");
    if (!k) return "";
    const lower = k.toLowerCase();
    if (k === "\x1b[A" || k === "\x1bOA" || lower === "up" || lower === "arrowup" || lower === "w" || lower === "k") {
      return "UP";
    }
    if (k === "\x1b[B" || k === "\x1bOB" || lower === "down" || lower === "arrowdown" || lower === "s" || lower === "j") {
      return "DOWN";
    }
    if (k === "\x1b[D" || k === "\x1bOD" || lower === "left" || lower === "arrowleft" || lower === "a" || lower === "h") {
      return "LEFT";
    }
    if (k === "\x1b[C" || k === "\x1bOC" || lower === "right" || lower === "arrowright" || lower === "d" || lower === "l") {
      return "RIGHT";
    }
    if (k === "." || k === " " || lower === "space") return "WAIT";
    if (k === "1") return "ITEM_1";
    if (k === "2") return "ITEM_2";
    if (k === "3") return "ITEM_3";
    if (k === ">" || k === "\r" || k === "\n" || lower === "enter" || lower === "return") return "STAIRS";
    if (lower === "r") return "RESTART";
    if (lower === "q" || k === "\x1b" || lower === "escape" || lower === "esc" || k === "\x03" || lower === "ctrl+c") {
      return "QUIT";
    }
    return "";
  }

  // Creates a fresh game state.
  function createGameState() {
    const state = {
      floor: 1,
      turn: 1,
      state: "PLAYING", // "PLAYING" | "VICTORY" | "DEAD"
      player: {
        x: 0,
        y: 0,
        hp: 32,
        maxHp: 32,
        baseAtk: 4,
        baseDef: 1,
        level: 1,
        xp: 0,
        nextXp: 20,
        gold: 0,
        weaponIdx: 0,
        armorIdx: 0,
        potions: 2,
        scrollFireball: 1,
        scrollTeleport: 1,
        kills: 0,
      },
      tiles: [],
      visible: [],
      explored: [],
      monsters: [],
      items: [],
      shrinesUsed: {},
      logs: [
        {
          text: "Welcome to the Dungeon of Perception! Retrieve the Amulet (✦) on Floor 5.",
          color: COLORS.logInfo,
        },
        {
          text: "Use [Arrows/WASD/HJKL] to move & attack, [1-3] for items, [>] for stairs.",
          color: COLORS.logMagic,
        },
      ],
    };
    generateFloor(state, 1);
    computeFOV(state);
    return state;
  }

  function addLog(state, text, color = COLORS.logInfo) {
    state.logs.push({ text, color });
    if (state.logs.length > 30) {
      state.logs.shift();
    }
  }

  function playerAtk(state) {
    return state.player.baseAtk + WEAPONS[state.player.weaponIdx].atk;
  }

  function playerDef(state) {
    return state.player.baseDef + ARMORS[state.player.armorIdx].def;
  }

  // Procedural room-and-corridor dungeon generator.
  function generateFloor(state, floorNum) {
    state.floor = floorNum;
    state.tiles = [];
    state.visible = [];
    state.explored = [];
    state.monsters = [];
    state.items = [];
    state.shrinesUsed = {};

    for (let y = 0; y < MAP_H; y++) {
      const tRow = [];
      const vRow = [];
      const eRow = [];
      for (let x = 0; x < MAP_W; x++) {
        tRow.push(TILE_WALL);
        vRow.push(false);
        eRow.push(false);
      }
      state.tiles.push(tRow);
      state.visible.push(vRow);
      state.explored.push(eRow);
    }

    const rooms = [];
    const targetRooms = randInt(6, 9);
    const maxAttempts = 80;

    for (let attempt = 0; attempt < maxAttempts && rooms.length < targetRooms; attempt++) {
      const w = randInt(6, 11);
      const h = randInt(4, 6);
      const x = randInt(1, MAP_W - w - 2);
      const y = randInt(1, MAP_H - h - 2);
      const candidate = {
        x,
        y,
        w,
        h,
        cx: Math.floor(x + w / 2),
        cy: Math.floor(y + h / 2),
      };

      let overlaps = false;
      for (let i = 0; i < rooms.length; i++) {
        const r = rooms[i];
        if (
          candidate.x - 1 < r.x + r.w &&
          candidate.x + candidate.w + 1 > r.x &&
          candidate.y - 1 < r.y + r.h &&
          candidate.y + candidate.h + 1 > r.y
        ) {
          overlaps = true;
          break;
        }
      }

      if (!overlaps) {
        for (let ry = candidate.y; ry < candidate.y + candidate.h; ry++) {
          for (let rx = candidate.x; rx < candidate.x + candidate.w; rx++) {
            state.tiles[ry][rx] = TILE_FLOOR;
          }
        }
        rooms.push(candidate);
      }
    }

    // Fallback room guarantee if random placement produced fewer than 2 rooms.
    if (rooms.length < 2) {
      const r1 = { x: 3, y: 3, w: 10, h: 5, cx: 8, cy: 5 };
      const r2 = { x: 36, y: 9, w: 12, h: 5, cx: 42, cy: 11 };
      rooms.push(r1, r2);
      for (const r of rooms) {
        for (let ry = r.y; ry < r.y + r.h; ry++) {
          for (let rx = r.x; rx < r.x + r.w; rx++) {
            state.tiles[ry][rx] = TILE_FLOOR;
          }
        }
      }
    }

    // Sort rooms left-to-right for natural progression across the floor.
    rooms.sort((a, b) => a.cx - b.cx);

    // Connect rooms with L-shaped corridors and place doors at room thresholds.
    for (let i = 1; i < rooms.length; i++) {
      const prev = rooms[i - 1];
      const curr = rooms[i];
      let x = prev.cx;
      let y = prev.cy;
      while (x !== curr.cx) {
        state.tiles[y][x] = TILE_FLOOR;
        x += x < curr.cx ? 1 : -1;
      }
      while (y !== curr.cy) {
        state.tiles[y][x] = TILE_FLOOR;
        y += y < curr.cy ? 1 : -1;
      }
      state.tiles[curr.cy][curr.cx] = TILE_FLOOR;
    }

    // Add doors `+` where a 1-tile corridor meets a room entrance.
    for (const r of rooms) {
      const candidates = [];
      for (let rx = r.x; rx < r.x + r.w; rx++) {
        if (r.y - 1 >= 1 && state.tiles[r.y - 1][rx] === TILE_FLOOR) {
          candidates.push({ x: rx, y: r.y - 1, horiz: true });
        }
        if (r.y + r.h < MAP_H - 1 && state.tiles[r.y + r.h][rx] === TILE_FLOOR) {
          candidates.push({ x: rx, y: r.y + r.h, horiz: true });
        }
      }
      for (let ry = r.y; ry < r.y + r.h; ry++) {
        if (r.x - 1 >= 1 && state.tiles[ry][r.x - 1] === TILE_FLOOR) {
          candidates.push({ x: r.x - 1, y: ry, horiz: false });
        }
        if (r.x + r.w < MAP_W - 1 && state.tiles[ry][r.x + r.w] === TILE_FLOOR) {
          candidates.push({ x: r.x + r.w, y: ry, horiz: false });
        }
      }
      for (const c of candidates) {
        if (Math.random() < 0.45) {
          const wallMatch = c.horiz
            ? state.tiles[c.y][c.x - 1] === TILE_WALL && state.tiles[c.y][c.x + 1] === TILE_WALL
            : state.tiles[c.y - 1][c.x] === TILE_WALL && state.tiles[c.y + 1][c.x] === TILE_WALL;
          if (wallMatch) {
            state.tiles[c.y][c.x] = TILE_DOOR;
          }
        }
      }
    }

    // Place player in the first room.
    state.player.x = rooms[0].cx;
    state.player.y = rooms[0].cy;

    // Place a healing shrine `▲` in a middle room on most floors.
    if (rooms.length >= 3) {
      const shrineRoom = rooms[Math.floor(rooms.length / 2)];
      const sx = clamp(shrineRoom.cx + 1, shrineRoom.x, shrineRoom.x + shrineRoom.w - 1);
      const sy = shrineRoom.cy;
      if (sx !== state.player.x || sy !== state.player.y) {
        state.tiles[sy][sx] = TILE_SHRINE;
      }
    }

    const lastRoom = rooms[rooms.length - 1];
    if (floorNum < MAX_FLOORS) {
      // Stairs down `>` in the final room.
      state.tiles[lastRoom.cy][lastRoom.cx] = TILE_STAIRS;
    } else {
      // Floor 5: The Elder Dragon (`D`) guards the Amulet of Perception (`✦`).
      state.items.push({
        x: lastRoom.cx,
        y: lastRoom.cy,
        type: "amulet",
        glyph: "✦",
        name: "Amulet of Perception",
        color: COLORS.amulet,
      });
      const bossX = clamp(lastRoom.cx - 1, lastRoom.x, lastRoom.x + lastRoom.w - 1);
      const bossY = lastRoom.cy;
      spawnMonster(state, "dragon", bossX, bossY);
    }

    // Populate rooms with monsters and items scaled to current floor.
    const eligibleMonsters = Object.keys(MONSTER_DEFS).filter((key) => {
      const m = MONSTER_DEFS[key];
      return !m.boss && floorNum >= m.minFloor && floorNum <= m.maxFloor;
    });

    for (let i = 1; i < rooms.length; i++) {
      const r = rooms[i];
      const count = randInt(1, floorNum >= 3 ? 2 : 1);
      for (let m = 0; m < count; m++) {
        const mx = randInt(r.x, r.x + r.w - 1);
        const my = randInt(r.y, r.y + r.h - 1);
        if (
          (mx === state.player.x && my === state.player.y) ||
          monsterAt(state, mx, my)
        ) {
          continue;
        }
        const kind = eligibleMonsters[randInt(0, eligibleMonsters.length - 1)];
        spawnMonster(state, kind, mx, my);
      }

      // Spawn floor loot.
      if (Math.random() < 0.75) {
        const ix = randInt(r.x, r.x + r.w - 1);
        const iy = randInt(r.y, r.y + r.h - 1);
        if (
          state.tiles[iy][ix] === TILE_FLOOR &&
          !itemAt(state, ix, iy) &&
          !(ix === state.player.x && iy === state.player.y)
        ) {
          spawnRandomItem(state, ix, iy, floorNum);
        }
      }
    }
  }

  function spawnMonster(state, kind, x, y) {
    const def = MONSTER_DEFS[kind];
    state.monsters.push({
      kind,
      glyph: def.glyph,
      name: def.name,
      color: def.color,
      x,
      y,
      hp: def.hp,
      maxHp: def.hp,
      atk: def.atk,
      def: def.def,
      xp: def.xp,
      ranged: !!def.ranged,
      heavyStrike: !!def.heavyStrike,
      phases: !!def.phases,
      ignoresArmor: !!def.ignoresArmor,
      boss: !!def.boss,
      turnCounter: 0,
    });
  }

  function spawnRandomItem(state, x, y, floorNum) {
    const roll = Math.random();
    if (roll < 0.30) {
      const amount = randInt(8 * floorNum, 18 * floorNum);
      state.items.push({
        x,
        y,
        type: "gold",
        glyph: "$",
        name: `${amount} Gold`,
        amount,
        color: COLORS.gold,
      });
    } else if (roll < 0.58) {
      state.items.push({
        x,
        y,
        type: "potion",
        glyph: "!",
        name: "Health Potion",
        color: COLORS.potion,
      });
    } else if (roll < 0.72) {
      state.items.push({
        x,
        y,
        type: "scroll_fire",
        glyph: "?",
        name: "Scroll of Fireball",
        color: COLORS.scrollFire,
      });
    } else if (roll < 0.82) {
      state.items.push({
        x,
        y,
        type: "scroll_tele",
        glyph: "?",
        name: "Scroll of Teleport",
        color: COLORS.scrollTele,
      });
    } else if (roll < 0.91) {
      const tier = clamp(Math.ceil((floorNum + randInt(0, 1)) / 1.5), 1, WEAPONS.length - 1);
      const wpn = WEAPONS[tier];
      state.items.push({
        x,
        y,
        type: "weapon",
        tier,
        glyph: "/",
        name: wpn.name,
        color: COLORS.weapon,
      });
    } else {
      const tier = clamp(Math.ceil((floorNum + randInt(0, 1)) / 1.5), 1, ARMORS.length - 1);
      const arm = ARMORS[tier];
      state.items.push({
        x,
        y,
        type: "armor",
        tier,
        glyph: "[",
        name: arm.name,
        color: COLORS.armor,
      });
    }
  }

  function monsterAt(state, x, y) {
    for (let i = 0; i < state.monsters.length; i++) {
      const m = state.monsters[i];
      if (m.hp > 0 && m.x === x && m.y === y) return m;
    }
    return null;
  }

  function itemAt(state, x, y) {
    for (let i = 0; i < state.items.length; i++) {
      if (state.items[i].x === x && state.items[i].y === y) return state.items[i];
    }
    return null;
  }

  // Raycast Field of View (radius 7) with Fog of War.
  function hasLineOfSight(state, x0, y0, x1, y1) {
    const dx = x1 - x0;
    const dy = y1 - y0;
    const steps = Math.max(Math.abs(dx), Math.abs(dy));
    if (steps === 0) return true;
    for (let s = 1; s < steps; s++) {
      const cx = Math.round(x0 + (dx * s) / steps);
      const cy = Math.round(y0 + (dy * s) / steps);
      if (cx < 0 || cx >= MAP_W || cy < 0 || cy >= MAP_H) return false;
      if (state.tiles[cy][cx] === TILE_WALL) return false;
    }
    return true;
  }

  function computeFOV(state) {
    for (let y = 0; y < MAP_H; y++) {
      for (let x = 0; x < MAP_W; x++) {
        state.visible[y][x] = false;
      }
    }
    const px = state.player.x;
    const py = state.player.y;
    for (let y = Math.max(0, py - FOV_RADIUS); y <= Math.min(MAP_H - 1, py + FOV_RADIUS); y++) {
      for (let x = Math.max(0, px - FOV_RADIUS); x <= Math.min(MAP_W - 1, px + FOV_RADIUS); x++) {
        const distSq = (x - px) * (x - px) + (y - py) * (y - py);
        if (distSq <= FOV_RADIUS * FOV_RADIUS + 2) {
          if (hasLineOfSight(state, px, py, x, y)) {
            state.visible[y][x] = true;
            state.explored[y][x] = true;
          }
        }
      }
    }
  }

  function checkLevelUp(state) {
    const p = state.player;
    while (p.xp >= p.nextXp) {
      p.xp -= p.nextXp;
      p.level++;
      p.nextXp = Math.floor(p.nextXp * 1.6);
      p.maxHp += 8;
      p.hp = p.maxHp;
      p.baseAtk += 2;
      if (p.level % 2 === 0) p.baseDef += 1;
      addLog(
        state,
        `LEVEL UP! You reached Level ${p.level}! (+8 Max HP, +2 ATK, full heal)`,
        COLORS.logVictory
      );
    }
  }

  function attackMonster(state, m) {
    const pAtk = playerAtk(state);
    const variance = randInt(-1, 2);
    const dmg = Math.max(1, pAtk - m.def + variance);
    m.hp -= dmg;
    if (m.hp <= 0) {
      m.hp = 0;
      state.player.xp += m.xp;
      state.player.kills++;
      addLog(
        state,
        `You strike ${m.name} for ${dmg} dmg — defeated! (+${m.xp} XP)`,
        COLORS.logCombat
      );
      checkLevelUp(state);
    } else {
      addLog(
        state,
        `You hit ${m.name} for ${dmg} dmg (${m.hp}/${m.maxHp} HP).`,
        COLORS.logCombat
      );
    }
  }

  function pickupItemsAtPlayer(state) {
    const p = state.player;
    for (let i = state.items.length - 1; i >= 0; i--) {
      const it = state.items[i];
      if (it.x === p.x && it.y === p.y) {
        state.items.splice(i, 1);
        if (it.type === "gold") {
          p.gold += it.amount;
          addLog(state, `Picked up ${it.name}.`, COLORS.logLoot);
        } else if (it.type === "potion") {
          p.potions++;
          addLog(state, `Picked up a Health Potion (!).`, COLORS.logLoot);
        } else if (it.type === "scroll_fire") {
          p.scrollFireball++;
          addLog(state, `Picked up a Scroll of Fireball (?).`, COLORS.logMagic);
        } else if (it.type === "scroll_tele") {
          p.scrollTeleport++;
          addLog(state, `Picked up a Scroll of Teleport (?).`, COLORS.logMagic);
        } else if (it.type === "weapon") {
          if (it.tier > p.weaponIdx) {
            p.weaponIdx = it.tier;
            addLog(
              state,
              `Equipped ${WEAPONS[it.tier].name} (+${WEAPONS[it.tier].atk} ATK)!`,
              COLORS.logLoot
            );
          } else {
            p.gold += 10 * it.tier;
            addLog(
              state,
              `Salvaged ${it.name} for ${10 * it.tier} Gold.`,
              COLORS.logLoot
            );
          }
        } else if (it.type === "armor") {
          if (it.tier > p.armorIdx) {
            p.armorIdx = it.tier;
            addLog(
              state,
              `Equipped ${ARMORS[it.tier].name} (+${ARMORS[it.tier].def} DEF)!`,
              COLORS.logLoot
            );
          } else {
            p.gold += 10 * it.tier;
            addLog(
              state,
              `Salvaged ${it.name} for ${10 * it.tier} Gold.`,
              COLORS.logLoot
            );
          }
        } else if (it.type === "amulet") {
          state.state = "VICTORY";
          addLog(
            state,
            "You claimed the Amulet of Perception (✦)! VICTORY IS YOURS!",
            COLORS.logVictory
          );
        }
      }
    }

    // Check healing shrine `▲`.
    const key = `${p.x},${p.y}`;
    if (state.tiles[p.y][p.x] === TILE_SHRINE && !state.shrinesUsed[key]) {
      state.shrinesUsed[key] = true;
      p.hp = p.maxHp;
      addLog(
        state,
        "The Shrine of Perception (▲) bathes you in light — HP fully restored!",
        COLORS.logLoot
      );
    }
  }

  function stepMonsters(state) {
    if (state.state !== "PLAYING") return;
    const p = state.player;

    for (const m of state.monsters) {
      if (m.hp <= 0) continue;
      m.turnCounter++;

      const dx = p.x - m.x;
      const dy = p.y - m.y;
      const manhattan = Math.abs(dx) + Math.abs(dy);
      const chebyshev = Math.max(Math.abs(dx), Math.abs(dy));

      // Boss Elder Dragon fiery breath attack at range 2–4 when in line of sight.
      if (m.boss && chebyshev >= 2 && chebyshev <= 4 && state.visible[m.y][m.x] && Math.random() < 0.5) {
        const breathDmg = Math.max(4, m.atk - Math.floor(playerDef(state) / 2) + randInt(-1, 3));
        p.hp -= breathDmg;
        addLog(
          state,
          `The ${m.name} unleashes FIERY BREATH for ${breathDmg} dmg!`,
          COLORS.logDanger
        );
        if (p.hp <= 0) {
          p.hp = 0;
          state.state = "DEAD";
          addLog(state, `You were incinerated by the ${m.name}...`, COLORS.logDanger);
          return;
        }
        continue;
      }

      // Skeleton Archer ranged arrow shot at distance 2–4 when visible.
      if (m.ranged && chebyshev >= 2 && chebyshev <= 4 && state.visible[m.y][m.x]) {
        const arrowDmg = Math.max(1, m.atk - playerDef(state) + randInt(-1, 1));
        p.hp -= arrowDmg;
        addLog(
          state,
          `${m.name} fires a bone arrow at you for ${arrowDmg} dmg!`,
          COLORS.logDanger
        );
        if (p.hp <= 0) {
          p.hp = 0;
          state.state = "DEAD";
          addLog(state, `You were slain by ${m.name}'s arrow...`, COLORS.logDanger);
          return;
        }
        continue;
      }

      // Adjacent melee attack.
      if (manhattan === 1) {
        let rawAtk = m.atk + randInt(-1, 2);
        let flavor = "attacks";
        if (m.heavyStrike && m.turnCounter % 2 === 0) {
          rawAtk = Math.floor(rawAtk * 1.5);
          flavor = "CRUSHES";
        }
        const effectiveDef = m.ignoresArmor ? state.player.baseDef : playerDef(state);
        const dmg = Math.max(1, rawAtk - effectiveDef);
        p.hp -= dmg;
        addLog(
          state,
          `${m.name} ${flavor} you for ${dmg} dmg${m.ignoresArmor ? " (ignores armor)" : ""}!`,
          COLORS.logDanger
        );
        if (p.hp <= 0) {
          p.hp = 0;
          state.state = "DEAD";
          addLog(state, `You have been slain by ${m.name} on Floor ${state.floor}.`, COLORS.logDanger);
          return;
        }
        continue;
      }

      // Chase player if within detection distance.
      if (chebyshev <= 8) {
        const stepX = dx === 0 ? 0 : dx > 0 ? 1 : -1;
        const stepY = dy === 0 ? 0 : dy > 0 ? 1 : -1;
        const candidates =
          Math.abs(dx) >= Math.abs(dy)
            ? [
                { x: m.x + stepX, y: m.y },
                { x: m.x, y: m.y + stepY },
              ]
            : [
                { x: m.x, y: m.y + stepY },
                { x: m.x + stepX, y: m.y },
              ];

        for (const c of candidates) {
          if (c.x < 1 || c.x >= MAP_W - 1 || c.y < 1 || c.y >= MAP_H - 1) continue;
          const tile = state.tiles[c.y][c.x];
          const passable = tile !== TILE_WALL || m.phases;
          if (passable && !monsterAt(state, c.x, c.y) && !(c.x === p.x && c.y === p.y)) {
            m.x = c.x;
            m.y = c.y;
            break;
          }
        }
      }
    }
  }

  function handlePlayerMove(state, dx, dy) {
    const p = state.player;
    const nx = p.x + dx;
    const ny = p.y + dy;
    if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) return false;

    const targetMonster = monsterAt(state, nx, ny);
    if (targetMonster) {
      attackMonster(state, targetMonster);
      return true;
    }

    if (state.tiles[ny][nx] === TILE_WALL) {
      addLog(state, "Solid dungeon stone blocks your path.", COLORS.sidebarLabel);
      return false;
    }

    p.x = nx;
    p.y = ny;
    pickupItemsAtPlayer(state);
    return true;
  }

  function useHealthPotion(state) {
    const p = state.player;
    if (p.potions <= 0) {
      addLog(state, "You have no Health Potions (!) left.", COLORS.sidebarLabel);
      return false;
    }
    if (p.hp >= p.maxHp) {
      addLog(state, "Your health is already full.", COLORS.sidebarLabel);
      return false;
    }
    p.potions--;
    const heal = Math.min(p.maxHp - p.hp, Math.max(14, Math.floor(p.maxHp * 0.55)));
    p.hp += heal;
    addLog(state, `You drink a Health Potion (!) and recover +${heal} HP!`, COLORS.logLoot);
    return true;
  }

  function useFireballScroll(state) {
    const p = state.player;
    if (p.scrollFireball <= 0) {
      addLog(state, "You have no Scrolls of Fireball (?) left.", COLORS.sidebarLabel);
      return false;
    }
    const visibleTargets = state.monsters.filter(
      (m) => m.hp > 0 && state.visible[m.y][m.x]
    );
    if (visibleTargets.length === 0) {
      addLog(state, "No visible enemies in sight to target with Fireball.", COLORS.sidebarLabel);
      return false;
    }
    p.scrollFireball--;
    let hitCount = 0;
    for (const m of visibleTargets) {
      const dmg = randInt(14, 22) + state.floor * 2;
      m.hp -= dmg;
      hitCount++;
      if (m.hp <= 0) {
        m.hp = 0;
        p.xp += m.xp;
        p.kills++;
      }
    }
    addLog(
      state,
      `Scroll of Fireball (?) engulfs ${hitCount} visible foe(s) in arcane flame!`,
      COLORS.logMagic
    );
    checkLevelUp(state);
    return true;
  }

  function useTeleportScroll(state) {
    const p = state.player;
    if (p.scrollTeleport <= 0) {
      addLog(state, "You have no Scrolls of Teleport (?) left.", COLORS.sidebarLabel);
      return false;
    }
    const openFloors = [];
    for (let y = 1; y < MAP_H - 1; y++) {
      for (let x = 1; x < MAP_W - 1; x++) {
        if (
          state.tiles[y][x] === TILE_FLOOR &&
          !monsterAt(state, x, y) &&
          (x !== p.x || y !== p.y)
        ) {
          openFloors.push({ x, y });
        }
      }
    }
    if (openFloors.length === 0) return false;
    p.scrollTeleport--;
    const dest = openFloors[randInt(0, openFloors.length - 1)];
    p.x = dest.x;
    p.y = dest.y;
    addLog(state, "You read a Scroll of Teleport (?) and blink across the dungeon!", COLORS.logMagic);
    pickupItemsAtPlayer(state);
    return true;
  }

  function descendStairs(state) {
    const p = state.player;
    if (state.tiles[p.y][p.x] !== TILE_STAIRS) {
      addLog(state, "Stand on the stairs down (>) to descend to the next floor.", COLORS.sidebarLabel);
      return false;
    }
    if (state.floor < MAX_FLOORS) {
      const nextFloor = state.floor + 1;
      generateFloor(state, nextFloor);
      computeFOV(state);
      if (nextFloor === MAX_FLOORS) {
        addLog(
          state,
          "Floor 5: You feel the scorching heat of the Elder Dragon (D) guarding the Amulet (✦)!",
          COLORS.logDanger
        );
      } else {
        addLog(
          state,
          `You descend the stone stairs to Floor ${nextFloor} of ${MAX_FLOORS}...`,
          COLORS.logInfo
        );
      }
    }
    return false;
  }

  // Renders a single map cell with TrueColor & Fog-of-War styling.
  function renderMapCell(state, x, y) {
    const isVis = state.visible[y][x];
    const isExp = state.explored[y][x];
    if (!isVis && !isExp) {
      return " ";
    }
    if (isVis) {
      if (state.player.x === x && state.player.y === y) {
        return `${COLORS.player}@${RESET}`;
      }
      const m = monsterAt(state, x, y);
      if (m) {
        return `${m.color}${m.glyph}${RESET}`;
      }
      const it = itemAt(state, x, y);
      if (it) {
        return `${it.color}${it.glyph}${RESET}`;
      }
      const t = state.tiles[y][x];
      if (t === TILE_WALL) return `${COLORS.wall}#${RESET}`;
      if (t === TILE_DOOR) return `${COLORS.door}+${RESET}`;
      if (t === TILE_STAIRS) return `${COLORS.stairs}>${RESET}`;
      if (t === TILE_SHRINE) {
        const used = state.shrinesUsed[`${x},${y}`];
        return `${used ? COLORS.fog : COLORS.shrine}▲${RESET}`;
      }
      return `${COLORS.floor}·${RESET}`;
    } else {
      const t = state.tiles[y][x];
      return `${COLORS.fog}${t}${RESET}`;
    }
  }

  // Builds the 17 inner rows of the 24x19 right-hand sidebar (22 visible chars per row).
  function buildSidebarRows(state) {
    const p = state.player;
    const rows = [];

    // Row 1: Floor & Turn
    const flStr = `Flr: ${state.floor}/${MAX_FLOORS}`;
    const trnStr = `Turn: ${state.turn}`;
    rows.push(
      `${COLORS.sidebarValue}${padRight(flStr, 10)}${RESET}${COLORS.sidebarLabel}${padRight(trnStr, 12)}${RESET}`
    );

    // Row 2: Level & XP
    const lvStr = `Lv: ${p.level}`;
    const xpStr = `XP: ${p.xp}/${p.nextXp}`;
    rows.push(
      `${COLORS.sidebarValue}${padRight(lvStr, 8)}${RESET}${COLORS.sidebarLabel}${padRight(xpStr, 14)}${RESET}`
    );

    // Row 3: HP readout
    const hpText = `HP: ${p.hp}/${p.maxHp}`;
    const hpRatio = p.maxHp > 0 ? p.hp / p.maxHp : 0;
    const hpColor =
      hpRatio > 0.55 ? COLORS.hpHigh : hpRatio > 0.25 ? COLORS.hpMid : COLORS.hpLow;
    rows.push(`${hpColor}${BOLD}${padRight(hpText, SIDE_W)}${RESET}`);

    // Row 4: Colored HP bar (22 chars: `[` + 20 blocks + `]`)
    const barWidth = 20;
    const filled = clamp(Math.round(hpRatio * barWidth), 0, barWidth);
    const empty = barWidth - filled;
    rows.push(
      `${COLORS.sidebarLabel}[${RESET}${hpColor}${"█".repeat(filled)}${COLORS.hpEmpty}${"░".repeat(empty)}${COLORS.sidebarLabel}]${RESET}`
    );

    // Row 5: ATK / DEF / Gold
    const statsLine = `ATK:${playerAtk(state)} DEF:${playerDef(state)} $${p.gold}`;
    rows.push(`${COLORS.sidebarValue}${padRight(statsLine, SIDE_W)}${RESET}`);

    // Row 6: Divider
    rows.push(`${COLORS.frame}${"─".repeat(SIDE_W)}${RESET}`);

    // Row 7: Equipped Weapon
    const wpn = WEAPONS[p.weaponIdx];
    rows.push(`${COLORS.weapon}${padRight(`/ ${wpn.name} (+${wpn.atk})`, SIDE_W)}${RESET}`);

    // Row 8: Equipped Armor
    const arm = ARMORS[p.armorIdx];
    rows.push(`${COLORS.armor}${padRight(`[ ${arm.name} (+${arm.def})`, SIDE_W)}${RESET}`);

    // Row 9: Divider
    rows.push(`${COLORS.frame}${"─".repeat(SIDE_W)}${RESET}`);

    // Row 10: Consumable Quick-Slots Header
    rows.push(`${COLORS.sidebarLabel}${padRight("Quick Items (1-3):", SIDE_W)}${RESET}`);

    // Rows 11-13: Quick slots [1], [2], [3]
    rows.push(
      `${COLORS.potion}${padRight(`[1] ! Heal Pot     x${p.potions}`, SIDE_W)}${RESET}`
    );
    rows.push(
      `${COLORS.scrollFire}${padRight(`[2] ? Fireball     x${p.scrollFireball}`, SIDE_W)}${RESET}`
    );
    rows.push(
      `${COLORS.scrollTele}${padRight(`[3] ? Teleport     x${p.scrollTeleport}`, SIDE_W)}${RESET}`
    );

    // Row 14: Divider
    rows.push(`${COLORS.frame}${"─".repeat(SIDE_W)}${RESET}`);

    // Row 15: Visible Enemies Header
    rows.push(`${COLORS.sidebarLabel}${padRight("Visible Foes:", SIDE_W)}${RESET}`);

    // Rows 16-17: Up to 2 closest visible enemies with HP readout
    const visMonsters = state.monsters
      .filter((m) => m.hp > 0 && state.visible[m.y][m.x])
      .sort((a, b) => {
        const da = Math.abs(a.x - p.x) + Math.abs(a.y - p.y);
        const db = Math.abs(b.x - p.x) + Math.abs(b.y - p.y);
        return da - db;
      });

    for (let i = 0; i < 2; i++) {
      if (i < visMonsters.length) {
        const m = visMonsters[i];
        const tag = `${m.glyph} ${m.name.slice(0, 11)}`;
        const hpInfo = `${m.hp}/${m.maxHp}`;
        const spaceCount = Math.max(1, SIDE_W - tag.length - hpInfo.length);
        const line = `${tag}${" ".repeat(spaceCount)}${hpInfo}`;
        rows.push(`${m.color}${padRight(line, SIDE_W)}${RESET}`);
      } else if (i === 0) {
        rows.push(`${COLORS.fog}${padRight("(No foes in sight)", SIDE_W)}${RESET}`);
      } else {
        rows.push(" ".repeat(SIDE_W));
      }
    }

    return rows;
  }

  // Renders an overlay banner inside the map viewport for Victory or Game Over.
  function renderMapRowWithOverlay(state, y) {
    let rowStr = "";
    for (let x = 0; x < MAP_W; x++) {
      rowStr += renderMapCell(state, x, y);
    }
    if (state.state === "PLAYING") return rowStr;

    if (y >= 5 && y <= 11) {
      const boxW = 38;
      const padLeft = Math.floor((MAP_W - boxW) / 2);
      const padRightCount = MAP_W - boxW - padLeft;
      let leftMap = "";
      let rightMap = "";
      for (let x = 0; x < padLeft; x++) leftMap += renderMapCell(state, x, y);
      for (let x = MAP_W - padRightCount; x < MAP_W; x++) rightMap += renderMapCell(state, x, y);

      const isWin = state.state === "VICTORY";
      const borderCol = isWin ? COLORS.logVictory : COLORS.logDanger;
      let bannerLine = "";
      if (y === 5) {
        bannerLine = `${borderCol}╔════════════════════════════════════╗${RESET}`;
      } else if (y === 6) {
        const title = isWin
          ? "   ✦ AMULET OF PERCEPTION CLAIMED ✦ "
          : "          ☠ YOU HAVE FALLEN ☠       ";
        bannerLine = `${borderCol}║${ title }║${RESET}`;
      } else if (y === 7) {
        const sub = isWin
          ? "   You conquered all 5 floors!      "
          : `   Slain on Floor ${state.floor} of ${MAX_FLOORS}            `;
        bannerLine = `${borderCol}║${RESET}${COLORS.sidebarValue}${sub}${RESET}${borderCol}║${RESET}`;
      } else if (y === 8) {
        const score = `   Lv:${state.player.level}  Gold:$${state.player.gold}  Foes:${state.player.kills}`;
        bannerLine = `${borderCol}║${RESET}${COLORS.logLoot}${padRight(score, 36)}${RESET}${borderCol}║${RESET}`;
      } else if (y === 9) {
        bannerLine = `${borderCol}║                                    ║${RESET}`;
      } else if (y === 10) {
        bannerLine = `${borderCol}║  Press [R] Restart or [Q/Esc] Quit ║${RESET}`;
      } else if (y === 11) {
        bannerLine = `${borderCol}╚════════════════════════════════════╝${RESET}`;
      }
      return leftMap + bannerLine + rightMap;
    }

    return rowStr;
  }

  // Composes and writes the complete 80x24 frame wrapped in DEC Mode 2026 sync sequences.
  function renderFrame(state) {
    const out = [];

    // Row 1: Top borders for Map (56 cols) + Sidebar (24 cols) = 80 cols
    const mapTitle = " Dungeon of Perception ";
    const mapTopDashes = "─".repeat(MAP_W - 1 - mapTitle.length);
    const sideTitle = " Status & Gear ";
    const sideTopDashes = "─".repeat(SIDE_W - 1 - sideTitle.length);
    out.push(
      `${COLORS.frame}╭─${RESET}${COLORS.frameTitle}${mapTitle}${RESET}${COLORS.frame}${mapTopDashes}╮╭─${RESET}${COLORS.frameTitle}${sideTitle}${RESET}${COLORS.frame}${sideTopDashes}╮${RESET}`
    );

    // Rows 2..18: 17 inner rows of Map + Sidebar
    const sideRows = buildSidebarRows(state);
    for (let y = 0; y < MAP_H; y++) {
      const mapRow = renderMapRowWithOverlay(state, y);
      const sideRow = sideRows[y] || " ".repeat(SIDE_W);
      out.push(
        `${COLORS.frame}│${RESET}${mapRow}${COLORS.frame}││${RESET}${sideRow}${COLORS.frame}│${RESET}`
      );
    }

    // Row 19: Bottom borders for Map (56 cols) + Sidebar (24 cols)
    out.push(
      `${COLORS.frame}╰${"─".repeat(MAP_W)}╯╰${"─".repeat(SIDE_W)}╯${RESET}`
    );

    // Row 20: Top border of 80x5 Combat & Event Log
    const logTitle = " Combat & Event Log ";
    const logTopDashes = "─".repeat(TOTAL_W - 3 - logTitle.length);
    out.push(
      `${COLORS.frame}╭─${RESET}${COLORS.frameTitle}${logTitle}${RESET}${COLORS.frame}${logTopDashes}╮${RESET}`
    );

    // Rows 21..23: Last 3 log entries (inner width 76 chars + 2 spaces + 2 borders = 80)
    const recentLogs = state.logs.slice(-3);
    while (recentLogs.length < 3) {
      recentLogs.unshift({ text: "", color: COLORS.logInfo });
    }
    for (let i = 0; i < 3; i++) {
      const entry = recentLogs[i];
      const text = padRight(entry.text, TOTAL_W - 4);
      out.push(
        `${COLORS.frame}│ ${RESET}${entry.color}${text}${RESET}${COLORS.frame} │${RESET}`
      );
    }

    // Row 24: Bottom border with controls legend (80 cols, no trailing newline)
    const legend =
      " [Arrows/WASD/HJKL] Move/Attack [.] Wait [1-3] Item [>] Stairs [R]estart [Q] ";
    const botDashes = "─".repeat(Math.max(0, TOTAL_W - 3 - legend.length));
    out.push(
      `${COLORS.frame}╰─${RESET}${COLORS.sidebarLabel}${legend}${RESET}${COLORS.frame}${botDashes}╯${RESET}`
    );

    const frame = `${SYNC_START}${HOME}${out.join("\n")}${SYNC_END}`;
    term.write(frame);
  }

  // Main game loop wrapped in try/finally to guarantee terminal state restoration.
  term.altScreen(true);
  term.rawMode(true);
  term.cursor(false);
  term.title("Dungeon of Perception — jsshell");

  try {
    let state = createGameState();
    renderFrame(state);

    let running = true;
    while (running) {
      const rawKey = term.readKey();
      if (!rawKey) continue;
      const action = normalizeKey(rawKey);
      if (!action) continue;

      if (action === "QUIT") {
        running = false;
        break;
      }

      if (action === "RESTART") {
        state = createGameState();
        renderFrame(state);
        continue;
      }

      if (state.state !== "PLAYING") {
        renderFrame(state);
        continue;
      }

      let turnTaken = false;
      if (action === "UP") {
        turnTaken = handlePlayerMove(state, 0, -1);
      } else if (action === "DOWN") {
        turnTaken = handlePlayerMove(state, 0, 1);
      } else if (action === "LEFT") {
        turnTaken = handlePlayerMove(state, -1, 0);
      } else if (action === "RIGHT") {
        turnTaken = handlePlayerMove(state, 1, 0);
      } else if (action === "WAIT") {
        addLog(state, "You steady your breath and watch the shadows...", COLORS.sidebarLabel);
        turnTaken = true;
      } else if (action === "ITEM_1") {
        turnTaken = useHealthPotion(state);
      } else if (action === "ITEM_2") {
        turnTaken = useFireballScroll(state);
      } else if (action === "ITEM_3") {
        turnTaken = useTeleportScroll(state);
      } else if (action === "STAIRS") {
        turnTaken = descendStairs(state);
      }

      if (turnTaken) {
        state.turn++;
        computeFOV(state);
        stepMonsters(state);
        computeFOV(state);
      }

      renderFrame(state);
    }
  } finally {
    term.cursor(true);
    term.rawMode(false);
    term.altScreen(false);
  }
})();
