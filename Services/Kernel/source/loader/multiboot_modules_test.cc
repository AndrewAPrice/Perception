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

#include "loader/multiboot_modules.h"
#include "testing.h"

using loader::ParseMultibootModuleName;

TEST(MultibootModulesParseNameTest) {
  // Test driver permissions string
  char* cmdline1 = (char*)"dl my_driver_process";
  size_t name_len1 = 0;
  bool is_driver1 = false;
  bool can_create_processes1 = false;
  bool can_set_focus1 = false;

  bool success1 = ParseMultibootModuleName(cmdline1, name_len1, is_driver1,
                                           can_create_processes1, can_set_focus1);
  ASSERT(success1, true);
  ASSERT(is_driver1, true);
  ASSERT(can_create_processes1, true);
  ASSERT(can_set_focus1, false);

  int match1 = 1;
  const char* expected1 = "my_driver_process";
  for (size_t i = 0; i < name_len1; i++) {
    if (cmdline1[i] != expected1[i]) {
      match1 = 0;
      break;
    }
  }
  ASSERT(match1, 1);

  // Test focus permission string
  char* cmdline2 = (char*)"f my_wm";
  size_t name_len2 = 0;
  bool is_driver2 = false;
  bool can_create_processes2 = false;
  bool can_set_focus2 = false;

  bool success2 = ParseMultibootModuleName(cmdline2, name_len2, is_driver2,
                                           can_create_processes2, can_set_focus2);
  ASSERT(success2, true);
  ASSERT(is_driver2, false);
  ASSERT(can_create_processes2, false);
  ASSERT(can_set_focus2, true);

  // Test standard application permissions string
  char* cmdline3 = (char*)"- my_app";
  size_t name_len3 = 0;
  bool is_driver3 = false;
  bool can_create_processes3 = false;
  bool can_set_focus3 = false;

  bool success3 = ParseMultibootModuleName(cmdline3, name_len3, is_driver3,
                                           can_create_processes3, can_set_focus3);
  ASSERT(success3, true);
  ASSERT(is_driver3, false);
  ASSERT(can_create_processes3, false);
  ASSERT(can_set_focus3, false);

  int match3 = 1;
  const char* expected3 = "my_app";
  for (size_t i = 0; i < name_len3; i++) {
    if (cmdline3[i] != expected3[i]) {
      match3 = 0;
      break;
    }
  }
  ASSERT(match3, 1);
}
