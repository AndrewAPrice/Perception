#ifndef TEST
#include "text_terminal.h"

#include "io.h"
#include "string_view.h"
#include "virtual_allocator.h"


// The text terminal is implemented by outputting over COM1.
namespace {

// The IO port to use.
constexpr unsigned short kPort = 0x3f8;  // COM1

// Charset for hexadecimal digits.
constexpr const char* kHexidecimalCharset = "0123456789ABCDEF";

// Default kernel print source attributes.
constexpr int kKernelPid = 0;
constexpr const char* kKernelName = "Kernel";
constexpr int kDefaultChannel = 0;

// Track current active ScopedPrintSource context.
ScopedPrintSource* current_print_source = nullptr;

// Track last emitted print source attributes.
int last_emitted_pid = -1;
const char* last_emitted_name = nullptr;
int last_emitted_channel = -1;

// Initialize the serial output.
void InitializeSerialOutput() {
  WriteIOByte(kPort + 1, 0x00);  // Disable all interrupts
  WriteIOByte(kPort + 3, 0x80);  // Enable DLAB (set baud rate divisor)
  WriteIOByte(kPort + 0, 0x03);  // Set divisor to 3 (lo byte) 38400 baud
  WriteIOByte(kPort + 1, 0x00);  //                  (hi byte)
  WriteIOByte(kPort + 3, 0x03);  // 8 bits, no parity, one stop bit
  WriteIOByte(kPort + 2,
              0xC7);  // Enable FIFO, clear them, with 14-byte threshold
  WriteIOByte(kPort + 4, 0x0B);  // IRQs enabled, RTS/DSR set
}

// Writes a single byte directly to serial COM1 without context checking.
void WriteSerialByte(char c) {
  while ((ReadIOByte(kPort + 5) & 0x20) == 0);
  WriteIOByte(kPort, c);
}

// Writes a null-terminated string directly to serial.
void WriteSerialString(const char* str) {
  if (str == nullptr) return;
  while (*str) {
    WriteSerialByte(*str);
    str++;
  }
}

// Emits decimal integer directly to serial.
void WriteSerialDecimal(int val) {
  if (val < 0) {
    WriteSerialByte('-');
    val = -val;
  }
  if (val == 0) {
    WriteSerialByte('0');
    return;
  }
  char temp[12];
  int idx = 0;
  while (val > 0) {
    temp[idx++] = '0' + (val % 10);
    val /= 10;
  }
  for (int i = idx - 1; i >= 0; i--) WriteSerialByte(temp[i]);
}

// Checks string equality.
bool StringsAreEqual(const char* a, const char* b) {
  if (a == b) return true;
  if (a == nullptr || b == nullptr) return false;
  while (*a && *b) {
    if (*a != *b) return false;
    a++;
    b++;
  }
  return *a == *b;
}

// Ensures the serial output stream has emitted the current source's control
// sequence.
void EnsurePrintSourceEmitted() {
  int target_pid = kKernelPid;
  const char* target_name = kKernelName;
  int target_channel = kDefaultChannel;

  if (current_print_source != nullptr) {
    target_pid = current_print_source->pid();
    target_name = current_print_source->name();
    target_channel = current_print_source->channel();
  }

  if (last_emitted_pid == target_pid &&
      StringsAreEqual(last_emitted_name, target_name) &&
      last_emitted_channel == target_channel)
    return;

  // Emit escape sequence \033]P;<pid>;<channel_id>;<name>\007
  WriteSerialByte('\033');
  WriteSerialByte(']');
  WriteSerialByte('P');
  WriteSerialByte(';');
  WriteSerialDecimal(target_pid);
  WriteSerialByte(';');
  WriteSerialDecimal(target_channel);
  WriteSerialByte(';');
  WriteSerialString(target_name ? target_name : kKernelName);
  WriteSerialByte('\007');

  last_emitted_pid = target_pid;
  last_emitted_name = target_name;
  last_emitted_channel = target_channel;
}

}  // namespace

ScopedPrintSource::ScopedPrintSource(int pid, const char* name, int channel)
    : pid_(pid), name_(name), channel_(channel) {
  previous_source_ = current_print_source;
  current_print_source = this;
}

ScopedPrintSource::~ScopedPrintSource() {
  current_print_source = previous_source_;
}

Printer::Printer() : number_format_(NumberFormat::Decimal) {}

// Prints a single character.
Printer& Printer::operator<<(char c) {
  EnsurePrintSourceEmitted();
  WriteSerialByte(c);
  return *this;
}

// Prints a null-terminated string.
Printer& Printer::operator<<(const char* str) {
  char* str1 = (char*)str;
  while (*str1) {
    *this << *str1;
    str1++;
  }
  return *this;
}

// Prints a string view.
Printer& Printer::operator<<(const StringView& str) {
  for (size_t i = 0; i < str.length; i++) *this << str.str[i];
  return *this;
}

// Prints an signed int.
Printer& Printer::operator<<(int c) {
  if (c < 0) {
    *this << "-";
    c = -c;
  }
  *this << (size_t)c;
  return *this;
}

// Prints a long int.
Printer& Printer::operator<<(size_t num) {
  switch (number_format_) {
    case NumberFormat::Decimal:
      PrintDecimal(num, /*with_commas=*/true);
      break;
    case NumberFormat::DecimalWithoutCommas:
      PrintDecimal(num, /*with_commas=*/false);
      break;
    case NumberFormat::Hexidecimal:
      PrintHexidecimal(num);
      break;
  }
  return *this;
}

// Switches to a new number format.
Printer& Printer::operator<<(NumberFormat format) {
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
    if (i == 11 || i == 7 || i == 3) *this << '-';
    *this << temp[i];
  }
}

// Prints a number as a decimal string.
void Printer::PrintDecimal(size_t n, bool with_commas) {
  if (n == 0) {
    *this << '0';
    return;
  }

  // The maximum 64-bit value is 18,446,744,073,709,551,615 which fits in 20
  // characters.
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
    if (with_commas &&
        (i == 1 || i == 4 || i == 7 || i == 10 || i == 13 || i == 16))
      *this << ',';
  }
}

// The singleton instance of the printer.
Printer print;

void InitializePrinter() {
  InitializeSerialOutput();
  // The kernel isn't set up for global constructors, so the printer must be
  // initialized explicitly.
  print = Printer();
}

#endif // TEST
