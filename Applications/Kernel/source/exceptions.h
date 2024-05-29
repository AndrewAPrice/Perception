// Copyright 2020 Google LLC
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

// The number of exceptions.
#define NUMBER_OF_EXCEPTIONS 32

// List of CPU exceptions. The values are the exception numbers reported by the
// CPU.
enum class Exception {
  DivisionByZero = 0,
  Debug = 1,
  NonMaskableInterrupt = 2,
  Breakpoint = 3,
  IntoDetectedOverflow = 4,
  OutOfBounds = 5,
  InvalidOpcode = 6,
  NoCoprocessor = 7,
  DoubleFault = 8,
  CoprocessorSegment = 9,
  BadTSS = 10,
  SegmentNotPreset = 11,
  StackFault = 12,
  GeneralProtectionFault = 13,
  PageFault = 14,
  UnknownInterrupt = 15,
  CoprocessorFault = 16,
  AlignmentCheck = 17,
  MachineCheck = 18
};

// Register the CPU exception interrupts.
extern void RegisterExceptionInterrupts();

// Returns the name for an exception.
const char* GetExceptionName(Exception exception);