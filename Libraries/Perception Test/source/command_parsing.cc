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
#include "command_parsing.h"

#include <string>
#include <string_view>
#include <vector>

namespace perception {
namespace testing {
namespace {

bool run_all_tests;
static std::vector<std::string> filters;

bool WildcardMatch(std::string_view pattern, std::string_view str) {
  if (pattern.empty()) {
    return str.empty();
  }

  if (pattern[0] == '*') {
    return WildcardMatch(pattern.substr(1), str) ||
           (!str.empty() && WildcardMatch(pattern, str.substr(1)));
  }

  if (!str.empty() && pattern[0] == str[0]) {
    return WildcardMatch(pattern.substr(1), str.substr(1));
  }

  return false;
}

}  // namespace

// Parse the command line arguments.
void ParseCommandLineArguments(int argc, char** argv) {
  if (argc <= 1) {
    run_all_tests = true;
    return;
  }

  run_all_tests = false;
  filters.clear();
  for (int i = 1; i < argc; ++i) {
    std::string_view arg(argv[i]);
    size_t start = 0;
    while (true) {
      size_t comma = arg.find(',', start);
      if (comma == std::string_view::npos) {
        if (start < arg.length()) {
          filters.emplace_back(arg.substr(start));
        }
        break;
      }
      if (comma > start) {
        filters.emplace_back(arg.substr(start, comma - start));
      }
      start = comma + 1;
    }
  }
}

// Whether the test should execute.
bool ShouldExecuteTest(std::string_view test_name) {
  if (run_all_tests) return true;

  for (const auto& filter : filters) {
    if (WildcardMatch(filter, test_name)) return true;
  }

  return false;
}

}  // namespace testing
}  // namespace perception