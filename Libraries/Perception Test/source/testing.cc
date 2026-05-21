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

#include "testing.h"

#include <iostream>
#include <string_view>
#include <vector>

#include "command_parsing.h"

namespace {

bool g_current_test_failed = false;

std::vector<::perception::testing::Task*>& GetSetupRegistry() {
  static std::vector<::perception::testing::Task*> registry;
  return registry;
}

std::vector<::perception::testing::Task*>& GetTestRegistry() {
  static std::vector<::perception::testing::Task*> registry;
  return registry;
}

// Reports the total number of test cases in the runner.
void ReportTotalTests(int count) {
  std::cout << "\033[?100;" << count << "T" << std::flush;
}

// Reports that a specific test case has started.
void ReportTestStart(std::string_view test_name) {
  std::cout << "\033[?101;" << test_name << "\x03" << std::flush;
}

// Reports that the currently running test case passed.
void ReportTestPass() { std::cout << "\033[?102;" << std::flush; }

// Reports that the currently running test case failed. Print any error messages
// before calling this.
void ReportTestFail() { std::cout << "\033[?103;" << std::flush; }

}  // namespace

namespace perception {
namespace testing {

void NotifyExpectFailed() { g_current_test_failed = true; }

void RegisterSetup(Task* setup) { GetSetupRegistry().push_back(setup); }
void RegisterTest(Task* test) { GetTestRegistry().push_back(test); }

}  // namespace testing
}  // namespace perception

int main(int argc, char** argv) {
  perception::testing::ParseCommandLineArguments(argc, argv);

  // Run setup tasks.
  std::vector<::perception::testing::Task*> setup_to_run;
  for (auto* setup : GetSetupRegistry()) {
    if (perception::testing::ShouldExecuteTest(setup->GetName()))
      setup_to_run.push_back(setup);
  }
  for (auto* setup : setup_to_run) setup->Run();

  // Filter out which test to run.
  std::vector<::perception::testing::Task*> test_to_run;
  for (auto* test : GetTestRegistry()) {
    if (perception::testing::ShouldExecuteTest(test->GetName()))
      test_to_run.push_back(test);
  }

  // Run filtered tests.
  ReportTotalTests(test_to_run.size());

  for (auto* test : test_to_run) {
    ReportTestStart(test->GetName());
    g_current_test_failed = false;

    try {
      test->Run();
      if (g_current_test_failed) {
        ReportTestFail();
      } else {
        ReportTestPass();
      }
    } catch (const ::perception::testing::AssertFailureException&) {
      std::cout << "Terminated due to a failed assert." << std::endl;
      ReportTestFail();
    } catch (const std::exception& e) {
      std::cout << "Terminated due to an exception: " << e.what() << std::endl;
      ReportTestFail();
    } catch (...) {
      std::cout << "Terminated due to an unknown exception." << std::endl;
      ReportTestFail();
    }
  }
  return 0;
}
