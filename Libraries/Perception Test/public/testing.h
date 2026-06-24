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

#pragma once
#include <limits.h>

#include <climits>
#ifndef INT_MAX
#define INT_MAX __INT_MAX__
#endif
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace perception {
namespace testing {

// The test interface that every test inherits from.
class Task {
 public:
  virtual ~Task() = default;
  virtual void Run() = 0;
  virtual std::string_view GetName() const = 0;
};

// Registers a setup step that runs before tests run.
void RegisterSetup(Task* setup);

// Registers a test to run.
void RegisterTest(Task* test);

// Exception thrown when an ASSERT macro fails.
class AssertFailureException : public std::exception {
 public:
  const char* what() const noexcept override { return "Assert failure"; }
};

// Notifies that an expectation failed.
void NotifyExpectFailed();

// Helper to print any value.
template <typename T>
void PrintValue(const T& val) {
  if constexpr (requires(std::ostream& os) { os << val; }) {
    std::cout << val;
  } else if constexpr (std::is_convertible_v<T, std::string_view>) {
    std::cout << std::string_view(val);
  } else {
    std::cout << "<unprintable value>";
  }
}

// Helper to print a failure message.
template <typename T1, typename T2>
void PrintFailure(std::string_view file, int line,
                  std::string_view expected_expr, std::string_view actual_expr,
                  const T1& expected, const T2& actual) {
  std::cout << file << ":" << line << ": Failure\n"
            << "  Expected: " << expected_expr << " (value: ";
  PrintValue(expected);
  std::cout << ")\n    Actual: " << actual_expr << " (value: ";
  PrintValue(actual);
  std::cout << ")\n" << std::flush;
}

// Helper for ASSERT.
template <typename T1, typename T2>
void PerformAssert(std::string_view file, int line,
                   std::string_view expected_expr, std::string_view actual_expr,
                   const T1& expected, const T2& actual) {
  if (!(expected == actual)) {
    PrintFailure(file, line, expected_expr, actual_expr, expected, actual);
    throw AssertFailureException();
  }
}

// Helper for EXPECT.
template <typename T1, typename T2>
void PerformExpect(std::string_view file, int line,
                   std::string_view expected_expr, std::string_view actual_expr,
                   const T1& expected, const T2& actual) {
  if (!(expected == actual)) {
    PrintFailure(file, line, expected_expr, actual_expr, expected, actual);
    NotifyExpectFailed();
  }
}

}  // namespace testing
}  // namespace perception

// Defines a startup block that runs before running any tests.
#define SETUP(SetupName)                                                 \
  class Setup_##SetupName##_Class : public ::perception::testing::Task { \
   public:                                                               \
    Setup_##SetupName##_Class() {                                        \
      ::perception::testing::RegisterSetup(this);                        \
    }                                                                    \
    void Run() override;                                                 \
    std::string_view GetName() const override { return #SetupName; }     \
  };                                                                     \
  static Setup_##SetupName##_Class Setup_##SetupName##_Instance;         \
  void Setup_##SetupName##_Class::Run()

// Defines a test to run.
#ifdef TEST
#undef TEST
#endif
#define TEST(TestName)                                                       \
  class Test_##TestName##_Class : public ::perception::testing::Task {       \
   public:                                                                   \
    Test_##TestName##_Class() { ::perception::testing::RegisterTest(this); } \
    void Run() override;                                                     \
    std::string_view GetName() const override { return #TestName; }          \
  };                                                                         \
  static Test_##TestName##_Class Test_##TestName##_Instance;                 \
  void Test_##TestName##_Class::Run()

#define ASSERT(expected, actual)                                               \
  ::perception::testing::PerformAssert(__FILE__, __LINE__, #expected, #actual, \
                                       expected, actual)

#define EXPECT(expected, actual)                                               \
  ::perception::testing::PerformExpect(__FILE__, __LINE__, #expected, #actual, \
                                       expected, actual)
