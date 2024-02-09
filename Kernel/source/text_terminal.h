#pragma once
#include "types.h"

// A wrapper around a non-null terminated string. StringView does not own the string, so the underlying
// data needs to stay in scope.
class StringView {
public:
    // Constructs a string view around "str" and "length".
    StringView(const char *str, size_t length) : str(str), length(length) {}

    // The source string.
    const char *str;

    // The length of the string.
    size_t length;
};

// Formats for printing numbers.
enum class NumberFormat {
    // A base 10 number, with commas.
    Decimal,
    // A base 10 number, without commas.
    DecimalWithCommas,
    // A hexidecimal number, starting with 0x.
    Hexidecimal
};

class Printer {
public:
    Printer();

    // Prints a single character.
    Printer& operator <<(char c);

    // Prints a null-terminated string.
    Printer& operator <<(const char *str);

    // Prints a string view.
    Printer& operator <<(StringView& str);

    // Prints an signed int.
    Printer& operator <<(int c);

    // Prints a long int.
    Printer& operator <<(size_t num);

    // Switches to a new number format.
    Printer& operator <<(NumberFormat format);

private:
    void PrintHexidecimal(size_t number);
    void PrintDecimal(size_t number, bool with_commas);

    // The current number format.
    NumberFormat number_format_;
};

// Singleton instance of the text printer.
extern Printer print;

// Initializes the text printer.
extern void InitializePrinter();
