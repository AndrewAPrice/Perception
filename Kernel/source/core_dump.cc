// Copyright 2024 Google LLC
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

#include "core_dump.h"

#include "io.h"
#include "process.h"
#include "text_terminal.h"
#include "thread.h"

namespace {

constexpr char kMonitorEscapeCode = '\xFF';
constexpr char kCoreDumpSequence[] = "CoreDump";

size_t CoreDumpSize(Process *process) {
  // TODO
  return 0;
}

void PrintCoreDumpContents(Process *process, Thread *target_thread,
                             int exception_no, size_t cr2, size_t error_code) {
  // TODO
}

}  // namespace

void PrintCoreDump(Process *process, Thread *target_thread, int exception_no,
                   size_t cr2, size_t error_code) {
  // Let the monitor know that a core dump is being output.
  print << kMonitorEscapeCode << kCoreDumpSequence << kMonitorEscapeCode
        << NumberFormat::DecimalWithoutCommas;
  // Print the length of the process name, followed by the name of the process.
  if (process == nullptr) {
    print << "0" << kMonitorEscapeCode;
  } else {
    print << (size_t)strlen(process->name) << kMonitorEscapeCode
          << process->name;
  }

  print << CoreDumpSize(process) << kMonitorEscapeCode;
  PrintCoreDumpContents(process, target_thread, exception_no, cr2, error_code);
}