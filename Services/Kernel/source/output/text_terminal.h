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

#include "types.h"

namespace common {
class StringView;
}

namespace output {

// Formats for printing numbers.
enum class NumberFormat {
  // A base 10 number, with commas.
  Decimal,
  // A base 10 number, without commas.
  DecimalWithoutCommas,
  // A hexidecimal number, starting with 0x.
  Hexidecimal
};

class Printer {
 public:
  Printer();

  // Prints a single character.
  Printer& operator<<(char c);

  // Prints a null-terminated string.
  Printer& operator<<(const char* str);

  // Prints a string view.
  Printer& operator<<(const common::StringView& str);

  // Prints an signed int.
  Printer& operator<<(int c);

  // Prints a long int.
  Printer& operator<<(size_t num);

  // Switches to a new number format.
  Printer& operator<<(NumberFormat format);

 private:
  void PrintHexidecimal(size_t number);
  void PrintDecimal(size_t number, bool with_commas);

  // The current number format.
  NumberFormat number_format_;
};

// RAII context for setting the current log source (process PID, process name,
// stream channel).
class ScopedPrintSource {
 public:
  // Sets the current print source for the duration of this object's scope.
  ScopedPrintSource(int pid, const char* name, int channel = 0);

  // Restores the previous print source when leaving scope.
  ~ScopedPrintSource();

  int pid() const { return pid_; }
  const char* name() const { return name_; }
  int channel() const { return channel_; }
  ScopedPrintSource* previous_source() const { return previous_source_; }

 private:
  ScopedPrintSource* previous_source_;
  int pid_;
  const char* name_;
  int channel_;
};

// Singleton instance of the text printer.
extern Printer print;

// Initializes the text printer.
void InitializePrinter();

}  // namespace output
