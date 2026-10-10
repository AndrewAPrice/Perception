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

#include "api_catalog.h"

namespace {

// Receiver name for top-level global lookups.
constexpr std::string_view kGlobalReceiver = "global";

// Receiver name for globalThis lookups.
constexpr std::string_view kGlobalThisReceiver = "globalThis";

// Receiver name for slash command lookups.
constexpr std::string_view kSlashReceiver = "slash";

// Receiver name for Command prototype lookups.
constexpr std::string_view kCommandReceiver = "Command";

// Receiver name for Pipeline prototype lookups.
constexpr std::string_view kPipelineReceiver = "Pipeline";

const ApiEntry* FindInVector(const std::vector<ApiEntry>& entries,
                             std::string_view name) {
  for (const auto& entry : entries) {
    if (entry.name == name) return &entry;
  }
  return nullptr;
}

}  // namespace

const std::vector<ApiEntry>& GetSlashCommandCatalog() {
  static const std::vector<ApiEntry> kCatalog = {
      {"/run", "/run <target> [args...]",
       "Launches an application, script, file, or directory in the foreground.",
       "void", true},
      {"/cd", "/cd [path]",
       "Changes or prints the current working directory.", "string", true},
      {"/pwd", "/pwd", "Prints the current working directory.", "string", true},
      {"/ls", "/ls [path]",
       "Lists directory entries and stores the result in _.", "DirEntry[]",
       true},
      {"/ps", "/ps [filter]",
       "Lists running processes and stores the result in _.", "ProcessInfo[]",
       true},
      {"/kill", "/kill <pid|name>",
       "Terminates matching processes by PID or name.", "number", true},
      {"/jobs", "/jobs",
       "Lists active background jobs started with .bg() in this session.",
       "ChildProcess[]", true},
      {"/memory", "/memory",
       "Opens the full-screen interactive Memory Explorer (Ctrl+M).", "void",
       true},
      {"/clear", "/clear", "Clears the terminal screen.", "void", true},
      {"/reset", "/reset",
       "Resets the QuickJS session and clears all user variables and history.",
       "void", true},
      {"/help", "/help",
       "Opens the API & Cookbook guide (/Applications/jsshell/api.md) in Viewer.",
       "void", true},
      {"/exit", "/exit", "Exits jsshell immediately.", "void", true},
  };
  return kCatalog;
}

const std::vector<ApiEntry>& GetGlobalCatalog() {
  static const std::vector<ApiEntry> kCatalog = {
      {"_", "_: any",
       "Holds the value of the most recent non-undefined REPL expression.",
       "any", false},
      {"run", "run(target: string, ...args: any[]): Command",
       "Launches an application, .js script, executable, file, or directory.",
       "Command", true},
      {"pipe", "pipe(...stages: Stage[]): Pipeline",
       "Chains commands, pipelines, or transform functions from left to right.",
       "Pipeline", true},
      {"fs", "namespace fs", "Filesystem and search operations.", "namespace",
       false},
      {"proc", "namespace proc", "Process inspection and job control.",
       "namespace", false},
      {"sys", "namespace sys",
       "System mounts, memory telemetry, environment, and power control.",
       "namespace", false},
      {"registry", "namespace registry",
       "System configuration registry access.", "namespace", false},
      {"clipboard", "namespace clipboard",
       "System clipboard read and write access.", "namespace", false},
      {"net", "namespace net",
       "Network interface inspection and DNS resolution.", "namespace", false},
      {"fetch", "fetch(url: string, options?: FetchOptions): Promise<Response>",
       "Performs an HTTP or HTTPS request.", "Promise<Response>", true},
      {"term", "namespace term",
       "Terminal display, cursor, alternate screen, raw input, and styling.",
       "namespace", false},
      {"sleep", "sleep(ms: number): Promise<void>",
       "Asynchronously sleeps for ms milliseconds.", "Promise<void>", true},
      {"print", "print(...values: any[]): void",
       "Prints values to stdout separated by spaces and a newline.", "void",
       true},
      {"JSON", "namespace JSON", "JSON parsing and serialization utilities.",
       "namespace", false},
      {"Math", "namespace Math",
       "Standard mathematical constants and functions.", "namespace", false},
      {"Object", "Object",
       "Standard JavaScript Object constructor and static methods.", "Object",
       true},
      {"Array", "Array",
       "Standard JavaScript Array constructor and static methods.", "Array",
       true},
      {"Promise", "Promise",
       "Standard JavaScript Promise constructor and combinators.", "Promise",
       true},
      {"Uint8Array", "Uint8Array", "Typed array of 8-bit unsigned integers.",
       "Uint8Array", true},
      {"Map", "Map", "Key-value collection preserving insertion order.", "Map",
       true},
      {"Set", "Set", "Collection of unique values.", "Set", true},
  };
  return kCatalog;
}

const std::vector<ApiEntry>& GetNamespaceCatalog(std::string_view ns_name) {
  static const std::vector<ApiEntry> kFsCatalog = {
      {"cwd", "fs.cwd(): string", "Returns the current working directory.",
       "string", true},
      {"chdir", "fs.chdir(path: string): void",
       "Changes the current working directory.", "void", true},
      {"readDir", "fs.readDir(path?: string): Promise<DirEntry[]>",
       "Lists directory entries.", "Promise<DirEntry[]>", true},
      {"stat", "fs.stat(path: string): Promise<FileInfo>",
       "Returns file or directory metadata and permissions.",
       "Promise<FileInfo>", true},
      {"exists", "fs.exists(path: string): Promise<boolean>",
       "Returns true if path exists.", "Promise<boolean>", true},
      {"readTextFile", "fs.readTextFile(path: string): Promise<string>",
       "Reads an entire file as a UTF-8 string.", "Promise<string>", true},
      {"readFile", "fs.readFile(path: string): Promise<Uint8Array>",
       "Reads an entire file as a Uint8Array.", "Promise<Uint8Array>", true},
      {"readLines", "fs.readLines(path: string): Promise<string[]>",
       "Reads a file and returns an array of lines.", "Promise<string[]>",
       true},
      {"readJson", "fs.readJson(path: string): Promise<any>",
       "Reads and parses a JSON file.", "Promise<any>", true},
      {"writeTextFile",
       "fs.writeTextFile(path: string, data: string, options?: { append?: "
       "boolean }): Promise<void>",
       "Writes or appends a UTF-8 string to path.", "Promise<void>", true},
      {"writeFile",
       "fs.writeFile(path: string, data: Uint8Array, options?: { append?: "
       "boolean }): Promise<void>",
       "Writes or appends binary bytes to path.", "Promise<void>", true},
      {"writeJson",
       "fs.writeJson(path: string, value: any, options?: { indent?: number }): "
       "Promise<void>",
       "Serializes value as formatted JSON and writes it to path.",
       "Promise<void>", true},
      {"copy",
       "fs.copy(src: string, dst: string, options?: { recursive?: boolean }): "
       "Promise<void>",
       "Copies a file or directory tree.", "Promise<void>", true},
      {"move", "fs.move(src: string, dst: string): Promise<void>",
       "Moves a file or directory.", "Promise<void>", true},
      {"remove",
       "fs.remove(path: string, options?: { recursive?: boolean }): "
       "Promise<void>",
       "Removes a file or directory.", "Promise<void>", true},
      {"mkdir",
       "fs.mkdir(path: string, options?: { recursive?: boolean }): "
       "Promise<void>",
       "Creates a directory.", "Promise<void>", true},
      {"readLink", "fs.readLink(path: string): Promise<string>",
       "Reads the target of a symbolic link.", "Promise<string>", true},
      {"glob", "fs.glob(pattern: string): Promise<string[]>",
       "Expands wildcard patterns (*, ?, **) and returns matching paths.",
       "Promise<string[]>", true},
      {"find",
       "fs.find(root?: string, filter?: string | RegExp | ((entry: DirEntry) "
       "=> boolean)): Promise<DirEntry[]>",
       "Recursively walks root and returns matching entries.",
       "Promise<DirEntry[]>", true},
      {"grep",
       "fs.grep(pattern: string | RegExp, path?: string): Promise<GrepMatch[]>",
       "Searches files under path for pattern matches.",
       "Promise<GrepMatch[]>", true},
      {"path", "namespace fs.path",
       "Pure synchronous path manipulation utilities.", "namespace", false},
  };

  static const std::vector<ApiEntry> kFsPathCatalog = {
      {"join", "fs.path.join(...segments: string[]): string",
       "Joins path segments and normalizes separators.", "string", true},
      {"resolve", "fs.path.resolve(...segments: string[]): string",
       "Resolves path segments into an absolute normalized path.", "string",
       true},
      {"dirname", "fs.path.dirname(path: string): string",
       "Returns the parent directory portion of path.", "string", true},
      {"basename", "fs.path.basename(path: string): string",
       "Returns the final path component.", "string", true},
      {"extname", "fs.path.extname(path: string): string",
       "Returns the file extension (including leading dot).", "string", true},
      {"normalize", "fs.path.normalize(path: string): string",
       "Normalizes a path by collapsing . and .. segments.", "string", true},
  };

  static const std::vector<ApiEntry> kProcCatalog = {
      {"ps", "proc.ps(filter?: string | number): Promise<ProcessInfo[]>",
       "Queries running processes and memory metrics.",
       "Promise<ProcessInfo[]>", true},
      {"kill", "proc.kill(target: number | string): Promise<number>",
       "Terminates matching processes by PID or name.", "Promise<number>",
       true},
      {"wait", "proc.wait(pid: number): Promise<void>",
       "Waits for the specified process ID to terminate.", "Promise<void>",
       true},
      {"apps", "proc.apps(): Promise<AppInfo[]>",
       "Lists installed applications discovered in /Applications.",
       "Promise<AppInfo[]>", true},
      {"jobs", "proc.jobs(): ChildProcess[]",
       "Lists background jobs started with .bg() in this session.",
       "ChildProcess[]", true},
      {"pid", "proc.pid: number", "Current jsshell process ID.", "number",
       false},
  };

  static const std::vector<ApiEntry> kPipeCatalog = {
      {"from", "pipe.from(data: string | Uint8Array | string[]): Pipeline",
       "Creates a Pipeline source from a string, Uint8Array, or line array.",
       "Pipeline", true},
      {"file", "pipe.file(path: string): Pipeline",
       "Creates a Pipeline source that streams the contents of path.",
       "Pipeline", true},
      {"seq", "pipe.seq(...sources: Stage[]): Pipeline",
       "Runs source stages sequentially, combining their stdout streams.",
       "Pipeline", true},
      {"merge", "pipe.merge(...sources: Stage[]): Pipeline",
       "Runs source stages concurrently into a shared stdout stream.",
       "Pipeline", true},
  };

  static const std::vector<ApiEntry> kSysCatalog = {
      {"mounts", "sys.mounts(): Promise<MountInfo[]>",
       "Lists mounted filesystems.", "Promise<MountInfo[]>", true},
      {"mount",
       "sys.mount(mountPoint: string, deviceName: string, options?: { offset?: "
       "number, length?: number }): Promise<void>",
       "Mounts a storage device at mountPoint.", "Promise<void>", true},
      {"unmount", "sys.unmount(mountPoint: string): Promise<void>",
       "Unmounts a filesystem.", "Promise<void>", true},
      {"remapMount",
       "sys.remapMount(oldMountPoint: string, newMountPoint: string): "
       "Promise<void>",
       "Remaps a mount point to a new path.", "Promise<void>", true},
      {"memory", "sys.memory(): MemoryStats",
       "Returns system, process, and QuickJS memory usage statistics.",
       "MemoryStats", true},
      {"gc", "sys.gc(): void", "Triggers QuickJS garbage collection.", "void",
       true},
      {"cores", "sys.cores(): number", "Returns the hardware CPU core count.",
       "number", true},
      {"uptime", "sys.uptime(): number",
       "Returns milliseconds elapsed since kernel startup.", "number", true},
      {"powerOff", "sys.powerOff(): Promise<void>",
       "Powers off the machine.", "Promise<void>", true},
      {"restart", "sys.restart(): Promise<void>", "Restarts the machine.",
       "Promise<void>", true},
      {"suspend", "sys.suspend(): Promise<void>", "Suspends the system.",
       "Promise<void>", true},
      {"wake", "sys.wake(): Promise<void>", "Wakes the system.",
       "Promise<void>", true},
      {"args", "sys.args: string[]",
       "Command-line arguments passed to the current script.", "string[]",
       false},
      {"env", "sys.env: Record<string, string>",
       "Environment variables map.", "Record<string, string>", false},
  };

  static const std::vector<ApiEntry> kRegistryCatalog = {
      {"get",
       "registry.get(corpus: \"applications\" | \"libraries\", namespace: "
       "string, key: string): Promise<any>",
       "Reads a value from the system registry.", "Promise<any>", true},
      {"set",
       "registry.set(corpus: \"applications\" | \"libraries\", namespace: "
       "string, key: string, value: any): Promise<void>",
       "Writes a value to the system registry.", "Promise<void>", true},
      {"remove",
       "registry.remove(corpus: \"applications\" | \"libraries\", namespace: "
       "string, key: string): Promise<void>",
       "Deletes a key from the system registry.", "Promise<void>", true},
      {"keys",
       "registry.keys(corpus: \"applications\" | \"libraries\", namespace: "
       "string): Promise<string[]>",
       "Lists all keys in a registry namespace.", "Promise<string[]>", true},
      {"namespaces",
       "registry.namespaces(): Promise<{ corpus: string, name: string }[]>",
       "Lists all namespaces in the system registry.",
       "Promise<{ corpus: string, name: string }[]>", true},
      {"flush", "registry.flush(): Promise<void>",
       "Flushes pending registry changes to disk.", "Promise<void>", true},
  };

  static const std::vector<ApiEntry> kClipboardCatalog = {
      {"get", "clipboard.get(): Promise<any>",
       "Reads the current value from the system clipboard.", "Promise<any>",
       true},
      {"set", "clipboard.set(value: any): Promise<void>",
       "Writes a string or value to the system clipboard.", "Promise<void>",
       true},
  };

  static const std::vector<ApiEntry> kNetCatalog = {
      {"interfaces", "net.interfaces(): Promise<NetInterface[]>",
       "Queries network interfaces, IP addresses, routers, and DNS servers.",
       "Promise<NetInterface[]>", true},
      {"resolve", "net.resolve(hostname: string): Promise<string[]>",
       "Resolves a hostname to IP addresses.", "Promise<string[]>", true},
  };

  static const std::vector<ApiEntry> kTermCatalog = {
      {"size",
       "term.size(): { cols: number, rows: number, widthPx: number, heightPx: "
       "number }",
       "Returns the current terminal dimensions.", "TermSize", true},
      {"clear", "term.clear(): void",
       "Clears the terminal screen and resets visible history rows.", "void",
       true},
      {"title", "term.title(title: string): void",
       "Sets the terminal window title.", "void", true},
      {"write", "term.write(text: string): void",
       "Writes raw text or ANSI sequences directly to stdout without a "
       "newline.",
       "void", true},
      {"moveTo", "term.moveTo(row: number, col: number): void",
       "Moves the terminal cursor to 1-based (row, col).", "void", true},
      {"cursor", "term.cursor(visible: boolean): void",
       "Shows or hides the terminal cursor.", "void", true},
      {"altScreen", "term.altScreen(enabled: boolean): void",
       "Enters or exits the alternate screen buffer.", "void", true},
      {"rawMode", "term.rawMode(enabled: boolean): void",
       "Enables or disables raw character-at-a-time keyboard input mode.",
       "void", true},
      {"readKey", "term.readKey(timeoutMs?: number): Promise<string | null>",
       "Reads and decodes a single key token from stdin with optional timeout.",
       "Promise<string | null>", true},
      {"style", "namespace term.style",
       "ANSI text styling helper functions.", "namespace", false},
  };

  static const std::vector<ApiEntry> kTermStyleCatalog = {
      {"bold", "term.style.bold(text: string): string",
       "Wraps text in ANSI bold styling.", "string", true},
      {"dim", "term.style.dim(text: string): string",
       "Wraps text in ANSI dim styling.", "string", true},
      {"red", "term.style.red(text: string): string",
       "Wraps text in ANSI red color.", "string", true},
      {"green", "term.style.green(text: string): string",
       "Wraps text in ANSI green color.", "string", true},
      {"yellow", "term.style.yellow(text: string): string",
       "Wraps text in ANSI yellow color.", "string", true},
      {"blue", "term.style.blue(text: string): string",
       "Wraps text in ANSI blue color.", "string", true},
      {"cyan", "term.style.cyan(text: string): string",
       "Wraps text in ANSI cyan color.", "string", true},
      {"magenta", "term.style.magenta(text: string): string",
       "Wraps text in ANSI magenta color.", "string", true},
      {"gray", "term.style.gray(text: string): string",
       "Wraps text in ANSI gray color.", "string", true},
  };

  static const std::vector<ApiEntry> kEmptyCatalog;

  if (ns_name == "fs") return kFsCatalog;
  if (ns_name == "fs.path") return kFsPathCatalog;
  if (ns_name == "proc") return kProcCatalog;
  if (ns_name == "pipe") return kPipeCatalog;
  if (ns_name == "sys") return kSysCatalog;
  if (ns_name == "registry") return kRegistryCatalog;
  if (ns_name == "clipboard") return kClipboardCatalog;
  if (ns_name == "net") return kNetCatalog;
  if (ns_name == "term") return kTermCatalog;
  if (ns_name == "term.style") return kTermStyleCatalog;
  return kEmptyCatalog;
}

const std::vector<ApiEntry>& GetCommandMethodCatalog() {
  static const std::vector<ApiEntry> kCatalog = {
      {"pipe", ".pipe(next: Stage | ((text: string) => any)): Pipeline",
       "Pipes stdout into another command, pipeline, or transform function.",
       "Pipeline", true},
      {"out", ".out(path: string, options?: { append?: boolean }): Pipeline",
       "Redirects stdout to a file (> or >>).", "Pipeline", true},
      {"err", ".err(path: string, options?: { append?: boolean }): Pipeline",
       "Redirects stderr to a file (2> or 2>>).", "Pipeline", true},
      {"errToOut", ".errToOut(): Pipeline",
       "Merges stderr into stdout (2>&1).", "Pipeline", true},
      {"nullOut", ".nullOut(): Pipeline", "Discards stdout (> /dev/null).",
       "Pipeline", true},
      {"nullErr", ".nullErr(): Pipeline", "Discards stderr (2> /dev/null).",
       "Pipeline", true},
      {"tee", ".tee(path: string, options?: { append?: boolean }): Pipeline",
       "Writes a copy of stdout to path while passing stdout downstream.",
       "Pipeline", true},
      {"text", ".text(options?: { trim?: boolean }): Promise<string>",
       "Executes the pipeline and resolves its captured stdout as a UTF-8 "
       "string.",
       "Promise<string>", true},
      {"lines", ".lines(): Promise<string[]>",
       "Executes the pipeline and resolves stdout as an array of lines.",
       "Promise<string[]>", true},
      {"json", ".json(): Promise<any>",
       "Executes the pipeline and parses its captured stdout as JSON.",
       "Promise<any>", true},
      {"bytes", ".bytes(): Promise<Uint8Array>",
       "Executes the pipeline and resolves its captured stdout as a "
       "Uint8Array.",
       "Promise<Uint8Array>", true},
      {"bg", ".bg(): ChildProcess",
       "Launches the command or pipeline in the background without waiting.",
       "ChildProcess", true},
      {"kill", ".kill(): Promise<number>",
       "Terminates the running command or pipeline.", "Promise<number>", true},
      {"then", ".then(onFulfilled, onRejected): Promise<void>",
       "Executes the command or pipeline and awaits completion.",
       "Promise<void>", true},
      {"catch", ".catch(onRejected): Promise<void>",
       "Attaches a rejection handler to the pipeline execution promise.",
       "Promise<void>", true},
      {"finally", ".finally(onFinally): Promise<void>",
       "Attaches a cleanup callback to the pipeline execution promise.",
       "Promise<void>", true},
  };
  return kCatalog;
}

const ApiEntry* FindApiEntry(std::string_view receiver, std::string_view name) {
  if (receiver.empty() || receiver == kGlobalReceiver ||
      receiver == kGlobalThisReceiver) {
    if (!name.empty() && name.front() == '/')
      return FindInVector(GetSlashCommandCatalog(), name);
    return FindInVector(GetGlobalCatalog(), name);
  }
  if (receiver == kSlashReceiver) {
    if (const ApiEntry* found = FindInVector(GetSlashCommandCatalog(), name))
      return found;
    std::string with_slash = "/";
    with_slash.append(name);
    return FindInVector(GetSlashCommandCatalog(), with_slash);
  }
  if (receiver == kCommandReceiver || receiver == kPipelineReceiver)
    return FindInVector(GetCommandMethodCatalog(), name);
  return FindInVector(GetNamespaceCatalog(receiver), name);
}
