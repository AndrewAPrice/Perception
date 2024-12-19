#pragma once
#include "types.h"

class StringView;

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
  Printer& operator<<(const StringView& str);

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

// Singleton instance of the text printer.
extern Printer print;

// Initializes the text printer.
void InitializePrinter();
