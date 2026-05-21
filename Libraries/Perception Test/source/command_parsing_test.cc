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

#include "testing.h"

TEST(ParseNoArguments) {
  char* argv[] = {(char*)"test_runner"};
  ::perception::testing::ParseCommandLineArguments(1, argv);

  EXPECT(true, ::perception::testing::ShouldExecuteTest("AnyTest"));
  EXPECT(true, ::perception::testing::ShouldExecuteTest("OtherTest"));
}

TEST(ParseExactMatch) {
  char* argv[] = {(char*)"test_runner", (char*)"MyTest"};
  ::perception::testing::ParseCommandLineArguments(2, argv);

  EXPECT(true, ::perception::testing::ShouldExecuteTest("MyTest"));
  EXPECT(false, ::perception::testing::ShouldExecuteTest("MyTest2"));
  EXPECT(false, ::perception::testing::ShouldExecuteTest("OtherTest"));
}

TEST(ParseMultipleArguments) {
  char* argv[] = {(char*)"test_runner", (char*)"TestA", (char*)"TestB"};
  ::perception::testing::ParseCommandLineArguments(3, argv);

  EXPECT(true, ::perception::testing::ShouldExecuteTest("TestA"));
  EXPECT(true, ::perception::testing::ShouldExecuteTest("TestB"));
  EXPECT(false, ::perception::testing::ShouldExecuteTest("TestC"));
}

TEST(ParseCommaSeparatedArguments) {
  char* argv[] = {(char*)"test_runner", (char*)"TestA,TestB"};
  ::perception::testing::ParseCommandLineArguments(2, argv);

  EXPECT(true, ::perception::testing::ShouldExecuteTest("TestA"));
  EXPECT(true, ::perception::testing::ShouldExecuteTest("TestB"));
  EXPECT(false, ::perception::testing::ShouldExecuteTest("TestC"));
}

TEST(ParseWildcardStarAtEnd) {
  char* argv[] = {(char*)"test_runner", (char*)"Test*"};
  ::perception::testing::ParseCommandLineArguments(2, argv);

  EXPECT(true, ::perception::testing::ShouldExecuteTest("Test"));
  EXPECT(true, ::perception::testing::ShouldExecuteTest("TestA"));
  EXPECT(true, ::perception::testing::ShouldExecuteTest("TestABC"));
  EXPECT(false, ::perception::testing::ShouldExecuteTest("Tst"));
  EXPECT(false, ::perception::testing::ShouldExecuteTest("OtherTest"));
}

TEST(ParseWildcardStarAtStart) {
  char* argv[] = {(char*)"test_runner", (char*)"*Test"};
  ::perception::testing::ParseCommandLineArguments(2, argv);

  EXPECT(true, ::perception::testing::ShouldExecuteTest("Test"));
  EXPECT(true, ::perception::testing::ShouldExecuteTest("ATest"));
  EXPECT(true, ::perception::testing::ShouldExecuteTest("SomeOtherTest"));
  EXPECT(false, ::perception::testing::ShouldExecuteTest("TestMore"));
}

TEST(ParseWildcardStarMiddle) {
  char* argv[] = {(char*)"test_runner", (char*)"A*B"};
  ::perception::testing::ParseCommandLineArguments(2, argv);

  EXPECT(true, ::perception::testing::ShouldExecuteTest("AB"));
  EXPECT(true, ::perception::testing::ShouldExecuteTest("AxB"));
  EXPECT(true, ::perception::testing::ShouldExecuteTest("AxxxxxxB"));
  EXPECT(false, ::perception::testing::ShouldExecuteTest("A"));
  EXPECT(false, ::perception::testing::ShouldExecuteTest("B"));
  EXPECT(false, ::perception::testing::ShouldExecuteTest("AxBx"));
}

TEST(ParseMultipleWildcards) {
  char* argv[] = {(char*)"test_runner", (char*)"*A*B*"};
  ::perception::testing::ParseCommandLineArguments(2, argv);

  EXPECT(true, ::perception::testing::ShouldExecuteTest("AB"));
  EXPECT(true, ::perception::testing::ShouldExecuteTest("xAxBx"));
  EXPECT(true, ::perception::testing::ShouldExecuteTest("xAxB"));
  EXPECT(true, ::perception::testing::ShouldExecuteTest("AxBx"));
  EXPECT(false, ::perception::testing::ShouldExecuteTest("BA"));
}
