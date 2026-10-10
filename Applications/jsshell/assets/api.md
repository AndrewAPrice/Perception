# jsshell — Perception JavaScript Shell & API Guide

Welcome to `jsshell`, Perception's interactive JavaScript shell and script runner powered by QuickJS. Every command at the prompt is either a concise **slash command** (starting with `/`) or valid **JavaScript** with top-level `await`, bidirectional process pipelines, and built-in OS namespaces.

> Tip: Click any blue link in this document to launch that game, application, or folder directly via Perception's Loader!

---

## Interactive Shortcuts & Shell Features

- **`_` (Last Result Variable)** — Holds the most recent non-`undefined` return value evaluated in the REPL.
- **`Tab` / `Shift+Tab` (Autocomplete)** — Completes slash commands (`/run`, `/cd`), application names and file paths (`/run <Tab>`, `run("<Tab>`), global variables, namespace members (`fs.<Tab>`, `pipe.<Tab>`, `term.<Tab>`), and chained pipeline methods (`run("...").<Tab>`).
- **`Up` / `Down` & Mouse Click (History & Object Inspector)** — Navigate past queries and response blocks across your session, or hover and click any visible block on screen. Selecting a **query** loads it into the prompt to edit; selecting a **response** opens the live value in the full-screen **Object Inspector** (`Enter` or click to expand/collapse nodes, `/` to filter, `c` to copy JSON to clipboard, `v` to bind a sub-property to a global variable, `q` or `Escape` to exit).
- **`Ctrl+M` or `/memory` (Memory Explorer)** — Opens the full-screen **Memory Explorer** showing all user REPL variables, cached history responses (`d` to free), built-in namespaces, live QuickJS + system RAM telemetry, and on-demand garbage collection (`g`).
- **`Ctrl+C` (Interrupt)** — Cancels the currently running JS evaluation or foreground command, or clears the current input line.
- **Return Value Formatting** — Expressions that return `undefined` (such as `/clear`, `term.clear()`, `/cd`, or foreground `run(...)` commands) print nothing and leave `_` unchanged. Strings print directly without quotes, and objects/arrays print a syntax-highlighted JSON preview.

### CLI Invocation Modes

- **`jsshell`** — Starts the interactive REPL session.
- **`jsshell <filepath> [args...]`** — Executes `<filepath>` as a JavaScript script with `[args...]` in `sys.args`, awaits all pending Promises, microtasks, and foreground pipelines, restores terminal state, and exits immediately.
- **`jsshell --script <code...>`** — Joins all arguments after `--script` with spaces as JavaScript code, evaluates it, awaits all pending Promises and pipelines to completion, and exits immediately.

---

## Sample Terminal Games

Click any link below to launch the game in a new Terminal window, or run the corresponding command inside `jsshell` to play directly inside your current terminal session:

- **[Launch Neon Snake](/Sample Scripts/snake.js)** — Classic arcade snake with 24-bit truecolor gradients, combo multipliers, and portal wrap mode. Run in shell: `/run "/Sample Scripts/snake.js"` or `run("/Sample Scripts/snake.js")`
- **[Launch Tetrominoes](/Sample Scripts/tetris.js)** — Guideline 7-bag falling block puzzle with SRS wall kicks, ghost piece, hold box, and line-clear flashes. Run in shell: `/run "/Sample Scripts/tetris.js"` or `run("/Sample Scripts/tetris.js")`
- **[Launch 2048 Puzzle](/Sample Scripts/2048.js)** — Sliding tile puzzle with 24-bit color cards, pop animations, and a 64-step undo stack (`U`). Run in shell: `/run "/Sample Scripts/2048.js"` or `run("/Sample Scripts/2048.js")`
- **[Launch Dungeon of Perception](/Sample Scripts/dungeon.js)** — Procedural 5-floor roguelike with raycast field-of-view lighting, tactical monster AI, shrines, and boss combat. Run in shell: `/run "/Sample Scripts/dungeon.js"` or `run("/Sample Scripts/dungeon.js")`
- **[Open Sample Scripts Folder in File Manager](/Sample Scripts)** — Browse all bundled `.js` scripts in `File Manager` (`run("/Sample Scripts")`).

---

## Cookbook: Running Programs, Scripts & Files

By default, `/run` and `run(...)` attach `jsshell`'s terminal `stdin`, `stdout`, and `stderr` pipes and wait in the foreground until the child process finishes.

```js
// Run an application with bash-style quoted arguments using /run:
/run "Terminal Test" --abc=f --abc="A F C"

// Equivalent call using the JavaScript run() function:
run("Terminal Test", "--abc=f", "--abc=A F C")

// Launch a bundled .js game directly inside the current terminal:
/run "/Sample Scripts/snake.js"
run("/Sample Scripts/tetris.js")

// Launch a GUI application in the background without waiting:
let job = run("Calculator").bg()
proc.jobs()

// Open a directory in File Manager or an image in Viewer:
run("/Applications/")
run("/Sample Images/1530779823.svg")

// Run interactive programs sequentially in a loop:
for (const demo of ["colors", "mouse", "graphics"]) {
  print(`--- Starting ${demo} ---`)
  await run("Terminal Test", "--demo", demo)
}
```

---

## Cookbook: Piping, Joining Pipes & String Conversions

`jsshell` makes it effortless to pipe processes together, filter streams through JavaScript functions, convert strings into pipes, and capture pipes back into JavaScript strings, lines, JSON, or bytes.

### 1. Piping Process A into Process B

```js
// Method chaining (ProgA | ProgB):
run("ProgA", "--raw").pipe(run("ProgB", "--filter"))

// Equivalent top-level pipe() helper:
pipe(
  run("ProgA", "--raw"),
  run("ProgB", "--filter"),
  run("ProgC")
)

// Pipe through a JavaScript transform function in the middle of a pipeline:
run("ProgA")
  .pipe(text => text.toUpperCase())
  .pipe(run("ProgB"))
```

### 2. Converting a String into a Pipe & a Pipe into a String

```js
// String -> Pipe: Feed a JavaScript string (or Uint8Array / string[]) into a program:
let input = "line 1\nline 2\nline 3\n"
pipe.from(input).pipe(run("ProgA"))

// Pipe -> String: Capture any program or pipeline output into a JS string:
let text = await run("ProgA").pipe(run("ProgB")).text()

// Pipe -> Lines / JSON / Bytes:
let lines = await run("ProgA").lines()
let data = await run("ProgA", "--json").json()
let bytes = await run("ProgA").bytes()

// Round-trip: String -> Pipe -> JS Transform -> Program -> String:
let result = await pipe.from("hello perception\n")
  .pipe(s => s.trim().toUpperCase())
  .pipe(run("ProgA"))
  .text()
```

### 3. Joining Multiple Pipes Sequentially & Concurrently

```js
// Sequential Join (pipe.seq): Runs each stage one after another in order
// and combines their output into a single string or downstream program:
let combinedText = await pipe.seq(
  pipe.from("=== Header ===\n"),
  pipe.file("/Applications/jsshell/launcher.json"),
  run("ProgA"),
  run("ProgB")
).text()

pipe.seq(run("ProgA"), run("ProgB")).pipe(run("ProgC"))

// Concurrent Join (pipe.merge): Runs all stages simultaneously sharing one
// output pipe into a string or downstream program:
let mergedText = await pipe.merge(run("ProgA"), run("ProgB")).text()
pipe.merge(run("ProgA"), run("ProgB")).pipe(run("ProgC"))
```

### 4. Piping From & To Files

```js
// Stream a file into a pipeline (< /input.txt | ProgA | ProgB > /output.txt):
pipe.file("/input.txt")
  .pipe(run("ProgA"))
  .pipe(run("ProgB"))
  .out("/output.txt")

// Append stdout to a file (>> /output.txt):
run("ProgA").out("/output.txt", { append: true })

// Redirect stderr to a file (2> /err.log) or merge stderr into stdout (2>&1):
run("ProgA").err("/err.log").out("/output.txt")
run("ProgA").errToOut().pipe(run("ProgB"))

// Tee stdout to a file while continuing the pipeline (| tee /mid.txt |):
let finalOut = await run("ProgA").tee("/mid.txt").pipe(run("ProgB")).text()
```

---

## Complete Reference: REPL Slash Commands

- **`/run <target> [args...]`** — Parses arguments using bash-style quoting (`"..."`, `'...'`, `\` escapes, and mid-token quotes like `--abc="A F C"`) and executes `run(target, ...args)` in the foreground.
- **`/cd [path]`** — Changes the current working directory via `fs.chdir(path)`, or prints `fs.cwd()` when `path` is omitted.
- **`/pwd`** — Prints the current working directory (`fs.cwd()`).
- **`/ls [path]`** — Lists directory entries via `fs.readDir(path)` and stores the result array in `_`.
- **`/ps [filter]`** — Lists running processes matching optional `filter` via `proc.ps(filter)` and stores the result in `_`.
- **`/kill <pid|name>`** — Terminates matching process(es) by PID or name via `proc.kill(target)`.
- **`/jobs`** — Lists active background jobs started with `.bg()` in this session (`proc.jobs()`).
- **`/memory`** — Opens the interactive full-screen **Memory Explorer** (`Ctrl+M`).
- **`/clear`** — Clears the terminal screen and visible history coordinates (`term.clear()`).
- **`/reset`** — Resets the entire QuickJS session, freeing all REPL variables and cached history objects, running garbage collection, and creating a fresh context.
- **`/help`** — Opens this guide (`/Applications/jsshell/api.md`) in Perception's Markdown `Viewer`.
- **`/exit`** — Exits `jsshell` immediately.

---

## Complete Reference: Global Functions & Pipelines

### Globals

- **`_`** — Holds the value of the last non-`undefined` expression evaluated in the REPL.
- **`run(target, ...args)`** — Launches an application name, `.js` script, executable path, directory, or file via `Loader::LaunchApplication`. Returns a Thenable `Command`.
- **`pipe(...stages)`** — Chains multiple stages (`Command`, `Pipeline`, or `(text) => string | Promise<string>`) from left to right.
- **`fetch(url, options?)`** — Performs an HTTP/HTTPS request (`{ method?, headers?, body? }`) and resolves to `{ status, ok, headers, text(), json(), bytes() }`.
- **`sleep(ms)`** — Asynchronously sleeps for `ms` milliseconds using Perception fibers.
- **`print(...values)`** — Prints values to `stdout` separated by spaces and followed by a newline.

### Pipe Combinators

- **`pipe.from(data)`** — Creates a `Pipeline` source from a `string`, `Uint8Array`, or array of lines (`string[]`).
- **`pipe.file(path)`** — Creates a `Pipeline` source that streams the contents of `path`.
- **`pipe.seq(...sources)`** — Runs each source stage sequentially in order and concatenates their outputs into a single `Pipeline`.
- **`pipe.merge(...sources)`** — Launches all source stages concurrently, merging their outputs into a single `Pipeline`.

### Command & Pipeline Methods

- **`.pipe(next)`** — Pipes `stdout` into another `Command`, `Pipeline`, or JS transform function `(text) => ...`.
- **`.text(options?)`** — Executes the pipeline, captures `stdout`, and resolves to a UTF-8 `string` (trims trailing newline unless `{ trim: false }`).
- **`.lines()`** — Executes the pipeline and resolves `stdout` as an array of lines (`string[]`).
- **`.json()`** — Executes the pipeline and parses `stdout` as JSON.
- **`.bytes()`** — Executes the pipeline and resolves `stdout` as a `Uint8Array`.
- **`.out(path, options?)`** — Redirects `stdout` to `path` (pass `{ append: true }` to append).
- **`.err(path, options?)`** — Redirects `stderr` to `path` (pass `{ append: true }` to append).
- **`.errToOut()`** — Merges `stderr` into `stdout` (`2>&1`).
- **`.nullOut()`** — Discards `stdout`.
- **`.nullErr()`** — Discards `stderr`.
- **`.tee(path, options?)`** — Writes a copy of `stdout` to `path` while passing `stdout` downstream.
- **`.bg()`** — Starts the command or pipeline in the background without waiting, returning `{ pid, name, running, wait(), kill() }`.
- **`.kill()`** — Terminates a running command or pipeline.

---

## Complete Reference: Built-in Namespaces

### 1. File System (`fs` & `fs.path`)

- **`fs.cwd()`** — Returns the current working directory synchronously.
- **`fs.chdir(path)`** — Changes the current working directory synchronously.
- **`fs.readDir(path?)`** — Resolves to `[{ name, path, type, size, isFile, isDirectory, isSymlink }, ...]`.
- **`fs.stat(path)`** — Resolves to `{ path, type, size, isReadable, isWritable, isExecutable, isFile, isDirectory }`.
- **`fs.exists(path)`** — Resolves to `true` if `path` exists, otherwise `false`.
- **`fs.readTextFile(path)`** — Reads an entire file as a UTF-8 `string`.
- **`fs.readFile(path)`** — Reads an entire file as a `Uint8Array`.
- **`fs.readLines(path)`** — Reads a text file and resolves to `string[]`.
- **`fs.readJson(path)`** — Reads and parses a JSON file.
- **`fs.writeTextFile(path, text, options?)`** — Writes or appends (`{ append: true }`) a UTF-8 string to `path`.
- **`fs.writeFile(path, bytes, options?)`** — Writes or appends (`{ append: true }`) a `Uint8Array` to `path`.
- **`fs.writeJson(path, value, options?)`** — Formats `value` as JSON (default `{ indent: 2 }`) and writes it to `path`.
- **`fs.copy(src, dst, options?)`** — Copies a file or directory (`{ recursive: true }`).
- **`fs.move(src, dst)`** — Moves or renames a file or directory.
- **`fs.remove(path, options?)`** — Deletes a file or directory (`{ recursive: true }`).
- **`fs.mkdir(path, options?)`** — Creates a directory (and parent directories when `{ recursive: true }`).
- **`fs.readLink(path)`** — Reads the target path of a symbolic link.
- **`fs.glob(pattern)`** — Expands wildcards (`*`, `?`, `**`) and resolves to matching file paths (`string[]`).
- **`fs.find(root?, filter?)`** — Recursively walks `root` and returns matching directory entries (filtered by substring, `RegExp`, or predicate function).
- **`fs.grep(pattern, path?)`** — Searches files under `path` for a string or `RegExp`, resolving to `[{ path, line, column, text }, ...]`.
- **`fs.path.join(...parts)`** — Joins path segments and normalizes `.` and `..`.
- **`fs.path.resolve(...parts)`** — Resolves path segments into an absolute path against `fs.cwd()`.
- **`fs.path.dirname(path)`** — Returns the parent directory portion of `path`.
- **`fs.path.basename(path)`** — Returns the final filename component of `path`.
- **`fs.path.extname(path)`** — Returns the file extension (including the leading `.`).
- **`fs.path.normalize(path)`** — Lexically normalizes redundant slashes, `.`, and `..` segments.

### 2. Process & Job Management (`proc`)

- **`proc.ps(filter?)`** — Lists running processes (`[{ pid, name, uniqueMemoryBytes, sharedMemoryBytes, services, cpuPercent }, ...]`), optionally filtered by substring or PID.
- **`proc.kill(target)`** — Terminates process(es) by numeric PID or exact/substring name and resolves to the count of terminated processes.
- **`proc.wait(pid)`** — Waits asynchronously until process `pid` terminates.
- **`proc.apps()`** — Lists installed applications discovered in `/Applications` (`[{ name, path, description, terminal }, ...]`).
- **`proc.jobs()`** — Returns active background jobs started via `.bg()` in the current session.
- **`proc.pid`** — Numeric process ID of the current `jsshell` instance.

### 3. System, Registry, Clipboard & Network (`sys`, `registry`, `clipboard`, `net`)

- **`sys.mounts()`** — Lists mounted filesystems (`[{ mountPoint, fileSystemType, deviceName, startByteOffset, byteLength, isWritable, isBootDrive }, ...]`).
- **`sys.mount(mountPoint, deviceName, options?)`** — Mounts a storage device at `mountPoint` (`{ offset?, length? }`).
- **`sys.unmount(mountPoint)`** — Unmounts the filesystem at `mountPoint`.
- **`sys.remapMount(oldMountPoint, newMountPoint)`** — Changes an existing mount path.
- **`sys.memory()`** — Returns live system, process, and QuickJS heap statistics (`{ totalSystemBytes, freeSystemBytes, processUniqueBytes, processSharedBytes, jsMallocSize, jsMallocCount, jsObjectCount, jsStringCount, jsAtomCount }`).
- **`sys.gc()`** — Triggers QuickJS garbage collection immediately.
- **`sys.cores()`** — Returns the number of CPU cores.
- **`sys.uptime()`** — Returns milliseconds elapsed since kernel startup.
- **`sys.powerOff()` / `sys.restart()` / `sys.suspend()` / `sys.wake()`** — Invokes system power state transitions.
- **`sys.args`** — Array of command-line arguments passed to the current `.js` script.
- **`sys.env`** — Object mapping environment variable names to values (`sys.env.PATH`, `sys.env.FOO = "bar"`).
- **`registry.get(corpus, namespace, key)`** — Reads a value from Perception's Registry (`corpus` is `"applications"` or `"libraries"`).
- **`registry.set(corpus, namespace, key, value)`** — Writes a value to the Registry.
- **`registry.remove(corpus, namespace, key)`** — Removes a key from the Registry.
- **`registry.keys(corpus, namespace)`** — Lists all keys inside a Registry namespace.
- **`registry.namespaces()`** — Lists all `{ corpus, name }` namespaces in the Registry.
- **`registry.flush()`** — Flushes pending Registry modifications to disk.
- **`clipboard.get()`** — Reads the current system clipboard value.
- **`clipboard.set(value)`** — Copies a string or value to the system clipboard.
- **`net.interfaces()`** — Lists network interfaces, MAC addresses, IPv4/IPv6 addresses, routers, and DNS servers.
- **`net.resolve(hostname)`** — Resolves a hostname to IP address strings.

### 4. Terminal & Interactive TUI Game API (`term`)

- **`term.size()`** — Returns `{ cols, rows, widthPx, heightPx }` for the current terminal viewport.
- **`term.clear()`** — Clears the screen and homes the cursor.
- **`term.title(title)`** — Sets the Terminal window title.
- **`term.write(text)`** — Writes raw text or ANSI escape sequences directly to `stdout` in a single atomic flush without a trailing newline.
- **`term.moveTo(row, col)`** — Moves the cursor to 1-based `(row, col)`.
- **`term.cursor(visible)`** — Shows (`true`) or hides (`false`) the text cursor.
- **`term.altScreen(enabled)`** — Enters (`true`) or exits (`false`) the alternate screen buffer (`?1049h` / `?1049l`). Automatically restored when a script finishes.
- **`term.rawMode(enabled)`** — Enables (`true`) or disables (`false`) raw character-at-a-time input without local echo. Automatically restored when a script finishes.
- **`term.readKey(timeoutMs?)`** — Reads a single normalized key name from the input queue (`"ArrowUp"`, `"ArrowDown"`, `"ArrowLeft"`, `"ArrowRight"`, `"Enter"`, `"Escape"`, `"Space"`, `"Backspace"`, `"Tab"`, `"PageUp"`, `"PageDown"`, `"Home"`, `"End"`, `"Delete"`, `"Ctrl+C"`, or single characters `"w"`, `"a"`, `"s"`, `"d"`, `"q"`, etc.). Pass `0` for non-blocking poll (`null` if no key), `> 0` to wait up to `timeoutMs` milliseconds, or omit to wait indefinitely.
- **`term.style`** — ANSI styling helpers: `term.style.bold(s)`, `dim(s)`, `red(s)`, `green(s)`, `yellow(s)`, `blue(s)`, `cyan(s)`, `magenta(s)`, and `gray(s)`.

#### Minimal Interactive Game Loop Template

```js
term.altScreen(true)
term.rawMode(true)
term.cursor(false)
try {
  let x = 10, y = 5
  while (true) {
    term.write("\x1b[?2026h\x1b[2J")
    term.moveTo(y, x)
    term.write(term.style.cyan("@"))
    term.moveTo(1, 2)
    term.write("Use WASD/Arrows to move, Q or Esc to quit")
    term.write("\x1b[?2026l")

    const key = await term.readKey(50)
    if (key === "q" || key === "Escape" || key === "Ctrl+C") break
    if (key === "ArrowLeft" || key === "a") x = Math.max(1, x - 1)
    if (key === "ArrowRight" || key === "d") x += 1
    if (key === "ArrowUp" || key === "w") y = Math.max(2, y - 1)
    if (key === "ArrowDown" || key === "s") y += 1
  }
} finally {
  term.cursor(true)
  term.rawMode(false)
  term.altScreen(false)
}
```
