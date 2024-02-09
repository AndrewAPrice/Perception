#include "text_terminal.h"

#include "io.h"
#include "string_view.h"
#include "virtual_allocator.h"

#ifdef __TEST__
#include <stdio.h>
#endif

// The text terminal is implemented by outputting over COM1.

namespace {
// The IO port to use.
constexpr unsigned short kPort = 0x3f8;  // COM1

static const char *kHexidecimalCharset = "0123456789ABCDEF";

// Initialize the serial output.
void InitializeSerialOutput() {
  outportb(kPort + 1, 0x00);  // Disable all interrupts
  outportb(kPort + 3, 0x80);  // Enable DLAB (set baud rate divisor)
  outportb(kPort + 0, 0x03);  // Set divisor to 3 (lo byte) 38400 baud
  outportb(kPort + 1, 0x00);  //                  (hi byte)
  outportb(kPort + 3, 0x03);  // 8 bits, no parity, one stop bit
  outportb(kPort + 2, 0xC7);  // Enable FIFO, clear them, with 14-byte threshold
  outportb(kPort + 4, 0x0B);  // IRQs enabled, RTS/DSR set
}

}

Printer::Printer() : number_format_(NumberFormat::Decimal) {}

// Prints a single character.
Printer& Printer::operator << (char c) {    
#ifdef __TEST__
  printf("%c", c);
#else
  while ((inportb(kPort + 5) & 0x20) == 0);
  outportb(kPort, c);
#endif
  return *this;
}

// Prints a null-terminated string.
Printer& Printer::operator <<(const char *str) {
  char *str1 = (char *)str;
  while (*str1) {
    *this << *str1;
    str1++;
  }
  return *this;
}

// Prints a string view.
Printer& Printer::operator <<(const StringView& str) {
  for (size_t i = 0; i < str.length; i++) *this << str.str[i];
  return *this;
}

// Prints an signed int.
Printer& Printer::operator <<(int c) {
  if (c < 0) {
    *this << "-";
    c = -c;
  }
  *this << (size_t)c;
  return *this;
}

// Prints a long int.
Printer& Printer::operator <<(size_t num) {
  switch (number_format_) {
    case NumberFormat::Decimal:
      PrintDecimal(num, /*with_commas=*/true);
    break;
    case NumberFormat::DecimalWithCommas:
      PrintDecimal(num, /*with_commas=*/false);
    break;
    case NumberFormat::Hexidecimal:
      PrintHexidecimal(num);
    break;
  }
  return *this;
}

// Switches to a new number format.
Printer& Printer::operator <<(NumberFormat format) {
  number_format_ = format;
  return *this;
}

// Prints a number as a 64-bit hexidecimal string.
void Printer::PrintHexidecimal(size_t h) {
  *this << "0x";
  char temp[16];
  size_t i;
  for (i = 0; i < 16; i++) {
    temp[i] = kHexidecimalCharset[h % 16];
    h /= 16;
  }
  for (int i = 15; i >= 0; i--) {
    if (i == 11 || i == 7 || i == 3)
      *this << '-';
    *this << temp[i];
  }
}

// Prints a number as a decimal string.
void Printer::PrintDecimal(size_t n, bool with_commas) {
  if (n == 0) {
    *this << '0';
    return;
  }

  // The maximum 64-bit value is 18,446,744,073,709,551,615 which fits in 20 characters.
  char temp[20];
  size_t first_char = 20;

  while (n > 0) {
    first_char--;
    temp[first_char] = '0' + (char)(n % 10);
    n /= 10;
  }

  size_t i;
  for (i = first_char; i < 20; i++) {
    *this << temp[i];
    if (with_commas && (i == 1 || i == 4 || i == 7 || i == 10 || i == 13 || i == 16))
      *this << ',';
  }
}

// The singleton instance of the printer.
Printer print;

void InitializePrinter() {
  InitializeSerialOutput();
  // The kernel isn't set up for global constructors, so the printer must be initialized
  // explicitly.
  print = Printer();
}

