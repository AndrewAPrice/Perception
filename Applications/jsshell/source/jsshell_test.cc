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

#include <string>
#include <vector>

#include "api_catalog.h"
#include "completion.h"
#include "slash_commands.h"
#include "testing.h"

namespace {

// Expected total number of REPL slash commands in the catalog.
constexpr size_t kExpectedSlashCommandCount = 12;

}  // namespace

TEST(ParseCliArgumentsReplMode) {
  const char* argv[] = {"jsshell"};
  ParsedCliArgs parsed = ParseCliArguments(1, argv);
  EXPECT(CliMode::kRepl, parsed.mode);
  EXPECT(std::string(""), parsed.code_or_path);
  EXPECT(size_t{0}, parsed.script_args.size());
}

TEST(ParseCliArgumentsInlineScriptMode) {
  const char* argv[] = {"jsshell", "--script", "let", "x", "=", "1 + 2;",
                        "print(x)"};
  ParsedCliArgs parsed = ParseCliArguments(7, argv);
  EXPECT(CliMode::kInlineScript, parsed.mode);
  EXPECT(std::string("let x = 1 + 2; print(x)"), parsed.code_or_path);
  EXPECT(size_t{0}, parsed.script_args.size());
}

TEST(ParseCliArgumentsFileScriptMode) {
  const char* argv[] = {"jsshell", "/Sample Scripts/snake.js", "--speed=fast",
                        "player1"};
  ParsedCliArgs parsed = ParseCliArguments(4, argv);
  EXPECT(CliMode::kFileScript, parsed.mode);
  EXPECT(std::string("/Sample Scripts/snake.js"), parsed.code_or_path);
  ASSERT(size_t{2}, parsed.script_args.size());
  EXPECT(std::string("--speed=fast"), parsed.script_args[0]);
  EXPECT(std::string("player1"), parsed.script_args[1]);
}

TEST(SlashCommandDetection) {
  EXPECT(true, IsSlashCommandInput("/run Calculator"));
  EXPECT(true, IsSlashCommandInput("  /help"));
  EXPECT(true, IsSlashCommandInput("/"));
  EXPECT(false, IsSlashCommandInput("// line comment"));
  EXPECT(false, IsSlashCommandInput("  /* block comment */"));
  EXPECT(false, IsSlashCommandInput("run(\"Calculator\")"));
  EXPECT(false, IsSlashCommandInput("   "));
}

TEST(SplitShellArgumentsBashQuoting) {
  std::vector<std::string> args =
      SplitShellArguments("\"Terminal Test\" --abc=f --abc=\"A F C\" 'single quoted' escaped\\ space");
  ASSERT(size_t{5}, args.size());
  EXPECT(std::string("Terminal Test"), args[0]);
  EXPECT(std::string("--abc=f"), args[1]);
  EXPECT(std::string("--abc=A F C"), args[2]);
  EXPECT(std::string("single quoted"), args[3]);
  EXPECT(std::string("escaped space"), args[4]);
}

TEST(ParseSlashCommandExtractsCommandAndArgs) {
  ParsedSlashCommand parsed =
      ParseSlashCommand("  /run \"/Sample Scripts/snake.js\" --abc=f --abc=\"A F C\"");
  EXPECT(true, parsed.is_slash_command);
  EXPECT(std::string("/run"), parsed.command);
  ASSERT(size_t{3}, parsed.args.size());
  EXPECT(std::string("/Sample Scripts/snake.js"), parsed.args[0]);
  EXPECT(std::string("--abc=f"), parsed.args[1]);
  EXPECT(std::string("--abc=A F C"), parsed.args[2]);
}

TEST(AnalyzeCompletionSlashCommandsAndArguments) {
  CompletionContext c1 = AnalyzeCompletionContext("/ru", 3);
  EXPECT(CompletionKind::kSlashCommand, c1.kind);
  EXPECT(std::string("/ru"), c1.prefix);
  EXPECT(size_t{0}, c1.replace_start);

  CompletionContext c2 = AnalyzeCompletionContext("/run Term", 9);
  EXPECT(CompletionKind::kSlashArgument, c2.kind);
  EXPECT(std::string("/run"), c2.receiver);
  EXPECT(std::string("Term"), c2.prefix);
  EXPECT(size_t{5}, c2.replace_start);
  EXPECT(false, c2.add_closing_quote);

  CompletionContext c3 =
      AnalyzeCompletionContext("/run \"/Sample Scripts/sn", 24);
  EXPECT(CompletionKind::kSlashArgument, c3.kind);
  EXPECT(std::string("/run"), c3.receiver);
  EXPECT(std::string("/Sample Scripts/sn"), c3.prefix);
  EXPECT(size_t{6}, c3.replace_start);
  EXPECT(true, c3.add_closing_quote);

  CompletionContext c4 = AnalyzeCompletionContext("/cd /App", 8);
  EXPECT(CompletionKind::kSlashArgument, c4.kind);
  EXPECT(std::string("/cd"), c4.receiver);
  EXPECT(std::string("/App"), c4.prefix);
  EXPECT(size_t{4}, c4.replace_start);
}

TEST(AnalyzeCompletionPropertiesAndChainedMethods) {
  CompletionContext c1 = AnalyzeCompletionContext("fs.read", 7);
  EXPECT(CompletionKind::kProperty, c1.kind);
  EXPECT(std::string("fs"), c1.receiver);
  EXPECT(std::string("read"), c1.prefix);
  EXPECT(size_t{3}, c1.replace_start);

  CompletionContext c2 = AnalyzeCompletionContext("fs.path.jo", 10);
  EXPECT(CompletionKind::kProperty, c2.kind);
  EXPECT(std::string("fs.path"), c2.receiver);
  EXPECT(std::string("jo"), c2.prefix);
  EXPECT(size_t{8}, c2.replace_start);

  CompletionContext c3 = AnalyzeCompletionContext("run(\"ProgA\").pi", 15);
  EXPECT(CompletionKind::kCommandMethod, c3.kind);
  EXPECT(std::string("Command"), c3.receiver);
  EXPECT(std::string("pi"), c3.prefix);
  EXPECT(size_t{13}, c3.replace_start);

  CompletionContext c4 =
      AnalyzeCompletionContext("pipe.seq(run(\"A\"), run(\"B\")).te", 31);
  EXPECT(CompletionKind::kCommandMethod, c4.kind);
  EXPECT(std::string("Command"), c4.receiver);
  EXPECT(std::string("te"), c4.prefix);
  EXPECT(size_t{29}, c4.replace_start);

  CompletionContext c5 = AnalyzeCompletionContext("let x = fet", 11);
  EXPECT(CompletionKind::kIdentifier, c5.kind);
  EXPECT(std::string("fet"), c5.prefix);
  EXPECT(size_t{8}, c5.replace_start);
}

TEST(AnalyzeCompletionStringLiterals) {
  CompletionContext c1 = AnalyzeCompletionContext("run(\"Term", 9);
  EXPECT(CompletionKind::kStringTarget, c1.kind);
  EXPECT(std::string("run"), c1.receiver);
  EXPECT(std::string("Term"), c1.prefix);
  EXPECT(size_t{5}, c1.replace_start);
  EXPECT(true, c1.add_closing_quote);

  CompletionContext c2 = AnalyzeCompletionContext("fs.readDir(\"/Sample", 19);
  EXPECT(CompletionKind::kStringPath, c2.kind);
  EXPECT(std::string("fs.readDir"), c2.receiver);
  EXPECT(std::string("/Sample"), c2.prefix);
  EXPECT(size_t{12}, c2.replace_start);
  EXPECT(true, c2.add_closing_quote);

  CompletionContext c3 =
      AnalyzeCompletionContext("run(\"ProgA\").out(\"/tmp/out", 26);
  EXPECT(CompletionKind::kStringPath, c3.kind);
  EXPECT(std::string(".out"), c3.receiver);
  EXPECT(std::string("/tmp/out"), c3.prefix);
  EXPECT(size_t{18}, c3.replace_start);
  EXPECT(true, c3.add_closing_quote);
}

TEST(JavaScriptInputCompleteness) {
  EXPECT(true, IsJavaScriptInputComplete("let x = 1 + 2"));
  EXPECT(true, IsJavaScriptInputComplete("run(\"A\").pipe(run(\"B\"))"));
  EXPECT(true, IsJavaScriptInputComplete("x++"));
  EXPECT(true, IsJavaScriptInputComplete("x--"));
  EXPECT(true, IsJavaScriptInputComplete("`template ${1 + 2} done`"));

  EXPECT(false, IsJavaScriptInputComplete("function test() {"));
  EXPECT(false, IsJavaScriptInputComplete("const arr = [1, 2,"));
  EXPECT(false, IsJavaScriptInputComplete("run(\"unclosed string"));
  EXPECT(false, IsJavaScriptInputComplete("`template ${1 + "));
  EXPECT(false, IsJavaScriptInputComplete("1 +"));
  EXPECT(false, IsJavaScriptInputComplete("a &&"));
  EXPECT(false, IsJavaScriptInputComplete("pipe.from(\"hi\")."));
  EXPECT(false, IsJavaScriptInputComplete("/* unclosed comment"));
}

TEST(TransformReplTopLevelDeclarationsPersistence) {
  std::string t1 = TransformReplTopLevelDeclarations("let x = 42");
  EXPECT(std::string("var x = (globalThis.x = 42)"), t1);

  std::string t2 = TransformReplTopLevelDeclarations("const a = 1, b = 2;");
  EXPECT(std::string("var a = (globalThis.a = 1), b = (globalThis.b = 2);"),
         t2);

  std::string t3 = TransformReplTopLevelDeclarations("let uninit;");
  EXPECT(std::string("var uninit = (globalThis.uninit = undefined);"), t3);

  std::string t4 =
      TransformReplTopLevelDeclarations("let s = \"hi\"; s.toUpperCase()");
  EXPECT(std::string("var s = (globalThis.s = \"hi\"); s.toUpperCase()"), t4);

  std::string t5 = TransformReplTopLevelDeclarations(
      "for (let i = 0; i < 3; i++) { const inner = i; }");
  EXPECT(std::string("for (let i = 0; i < 3; i++) { const inner = i; }"), t5);
}

TEST(NormalizeLexicalPathAndGlobMatching) {
  EXPECT(std::string("/Applications"),
         NormalizeLexicalPath("/", "Applications"));
  EXPECT(std::string("/Sample Scripts/snake.js"),
         NormalizeLexicalPath("/Applications", "../Sample Scripts/./snake.js"));
  EXPECT(std::string("/"), NormalizeLexicalPath("/a/b", "../../.."));
  EXPECT(std::string("/x/z"), NormalizeLexicalPath("/a", "/x//y/../z/"));

  EXPECT(true,
         MatchesGlobPattern("/Sample Scripts/*.js", "/Sample Scripts/snake.js"));
  EXPECT(false,
         MatchesGlobPattern("/Sample Scripts/*.js", "/Sample Scripts/a/b.js"));
  EXPECT(true, MatchesGlobPattern("/**/*.js", "/Sample Scripts/snake.js"));
  EXPECT(true, MatchesGlobPattern("/**/*.js", "/snake.js"));
  EXPECT(true, MatchesGlobPattern("????.js", "2048.js"));
  EXPECT(false, MatchesGlobPattern("????.js", "snake.js"));
}

TEST(ApiCatalogCompletenessAndLookups) {
  EXPECT(kExpectedSlashCommandCount, GetSlashCommandCatalog().size());
  EXPECT(false, GetGlobalCatalog().empty());
  EXPECT(false, GetNamespaceCatalog("fs").empty());
  EXPECT(false, GetNamespaceCatalog("fs.path").empty());
  EXPECT(false, GetNamespaceCatalog("proc").empty());
  EXPECT(false, GetNamespaceCatalog("pipe").empty());
  EXPECT(false, GetNamespaceCatalog("sys").empty());
  EXPECT(false, GetNamespaceCatalog("registry").empty());
  EXPECT(false, GetNamespaceCatalog("clipboard").empty());
  EXPECT(false, GetNamespaceCatalog("net").empty());
  EXPECT(false, GetNamespaceCatalog("term").empty());
  EXPECT(false, GetNamespaceCatalog("term.style").empty());
  EXPECT(false, GetCommandMethodCatalog().empty());

  const ApiEntry* run_slash = FindApiEntry("slash", "/run");
  ASSERT(true, run_slash != nullptr);
  EXPECT(std::string("/run"), run_slash->name);

  const ApiEntry* run_global = FindApiEntry("", "run");
  ASSERT(true, run_global != nullptr);
  EXPECT(std::string("Command"), run_global->return_type);

  const ApiEntry* pipe_seq = FindApiEntry("pipe", "seq");
  ASSERT(true, pipe_seq != nullptr);
  EXPECT(std::string("Pipeline"), pipe_seq->return_type);

  const ApiEntry* cmd_text = FindApiEntry("Command", "text");
  ASSERT(true, cmd_text != nullptr);
  EXPECT(std::string("Promise<string>"), cmd_text->return_type);
}
