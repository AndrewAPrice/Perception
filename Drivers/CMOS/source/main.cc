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

// CMOS index/address I/O port.
constexpr uint16 kCmosAddressPort = 0x70;
// CMOS data I/O port.
constexpr uint16 kCmosDataPort = 0x71;
// CMOS register for seconds.
constexpr uint8 kCmosRegSeconds = 0x00;
// CMOS register for minutes.
constexpr uint8 kCmosRegMinutes = 0x02;
// CMOS register for hours.
constexpr uint8 kCmosRegHours = 0x04;
// CMOS register for day of month.
constexpr uint8 kCmosRegDay = 0x07;
// CMOS register for month.
constexpr uint8 kCmosRegMonth = 0x08;
// CMOS register for year within century.
constexpr uint8 kCmosRegYear = 0x09;
// CMOS status register A.
constexpr uint8 kCmosRegStatusA = 0x0A;
// CMOS status register B.
constexpr uint8 kCmosRegStatusB = 0x0B;
// CMOS century register.
constexpr uint8 kCmosRegCentury = 0x32;
// Bit in status register A indicating an RTC update is in progress.
constexpr uint8 kStatusAUpdateInProgressBit = 0x80;
// Bit in status register B indicating 24-hour mode (0 = 12-hour, 1 = 24-hour).
constexpr uint8 kStatusB24HourModeBit = 0x02;
// Bit in status register B indicating binary mode (0 = BCD, 1 = binary).
constexpr uint8 kStatusBBinaryModeBit = 0x04;
// Bit in the hours register indicating PM in 12-hour mode.
constexpr int kHourPmBit = 0x80;
// Mask for the hour value excluding the PM flag bit.
constexpr int kHourValueMask = 0x7F;
// Number of hours in a half day (12-hour clock).
constexpr int kHoursPerHalfDay = 12;

uint8 ReadCmosRegister(uint8 reg) {
  perception::Write8BitsToPort(kCmosAddressPort, reg);
  return perception::Read8BitsFromPort(kCmosDataPort);
}

bool IsCmosUpdateInProgress() {
  return (ReadCmosRegister(kCmosRegStatusA) & kStatusAUpdateInProgressBit) != 0;
}

void WaitForCmosUpdate() {
  while (IsCmosUpdateInProgress())
    perception::SleepForDuration(std::chrono::milliseconds(1));
}

}  // namespace

int main() {
  int second, minute, hour, day, month, year;

  // Read CMOS
  WaitForCmosUpdate();
  second = ReadCmosRegister(kCmosRegSeconds);
  minute = ReadCmosRegister(kCmosRegMinutes);
  hour = ReadCmosRegister(kCmosRegHours);
  day = ReadCmosRegister(kCmosRegDay);
  month = ReadCmosRegister(kCmosRegMonth);
  year = ReadCmosRegister(kCmosRegYear);

  uint8 registerB = ReadCmosRegister(kCmosRegStatusB);

  // Convert BCD to binary if necessary
  if (!(registerB & kStatusBBinaryModeBit)) {
    second = (second & 0x0F) + ((second / 16) * 10);
    minute = (minute & 0x0F) + ((minute / 16) * 10);
    hour =
        ((hour & 0x0F) + (((hour & 0x70) / 16) * 10)) | (hour & kHourPmBit);
    day = (day & 0x0F) + ((day / 16) * 10);
    month = (month & 0x0F) + ((month / 16) * 10);
    year = (year & 0x0F) + ((year / 16) * 10);
  }

  // Convert 12-hour clock to 24-hour clock if necessary
  if (!(registerB & kStatusB24HourModeBit)) {
    bool is_pm = (hour & kHourPmBit) != 0;
    hour &= kHourValueMask;
    if (hour == kHoursPerHalfDay) hour = 0;
    if (is_pm) hour += kHoursPerHalfDay;
  }

  // Calculate year
  uint8 century_reg = ReadCmosRegister(kCmosRegCentury);
  if (!(registerB & kStatusBBinaryModeBit))
    century_reg = (century_reg & 0x0F) + ((century_reg / 16) * 10);
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
