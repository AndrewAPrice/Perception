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

#include <time.h>

#include <iostream>

#include "perception/port_io.h"
#include "perception/time.h"

namespace {

uint8 ReadCmosRegister(uint8 reg) {
  perception::Write8BitsToPort(0x70, reg);
  return perception::Read8BitsFromPort(0x71);
}

bool IsCmosUpdateInProgress() { return (ReadCmosRegister(0x0A) & 0x80) != 0; }

void WaitForCmosUpdate() {
  while (IsCmosUpdateInProgress()) {
    perception::SleepForDuration(std::chrono::milliseconds(1));
  }
}

}  // namespace

int main() {
  int second, minute, hour, day, month, year;

  // Read CMOS
  WaitForCmosUpdate();
  second = ReadCmosRegister(0x00);
  minute = ReadCmosRegister(0x02);
  hour = ReadCmosRegister(0x04);
  day = ReadCmosRegister(0x07);
  month = ReadCmosRegister(0x08);
  year = ReadCmosRegister(0x09);

  uint8 registerB = ReadCmosRegister(0x0B);

  // Convert BCD to binary if necessary
  if (!(registerB & 0x04)) {
    second = (second & 0x0F) + ((second / 16) * 10);
    minute = (minute & 0x0F) + ((minute / 16) * 10);
    hour = ((hour & 0x0F) + (((hour & 0x70) / 16) * 10)) | (hour & 0x80);
    day = (day & 0x0F) + ((day / 16) * 10);
    month = (month & 0x0F) + ((month / 16) * 10);
    year = (year & 0x0F) + ((year / 16) * 10);
  }

  // Convert 12-hour clock to 24-hour clock if necessary
  if (!(registerB & 0x02) && (hour & 0x80)) {
    hour = ((hour & 0x7F) + 12) % 24;
  }

  // Calculate year
  uint8 century_reg = ReadCmosRegister(0x32);
  if (!(registerB & 0x04)) {
    century_reg = (century_reg & 0x0F) + ((century_reg / 16) * 10);
  }
  if (century_reg > 0) {
    year += century_reg * 100;
  } else {
    if (year < 70)
      year += 2000;
    else
      year += 1900;
  }

  struct tm time_struct;
  time_struct.tm_sec = second;
  time_struct.tm_min = minute;
  time_struct.tm_hour = hour;
  time_struct.tm_mday = day;
  time_struct.tm_mon = month - 1;
  time_struct.tm_year = year - 1900;
  time_struct.tm_isdst = 0;

  time_t utc_seconds = timegm(&time_struct);
  uint64 utc_microseconds = (uint64)utc_seconds * 1000000ULL;

  perception::SetTimeInfo(utc_microseconds);

  return 0;
}
