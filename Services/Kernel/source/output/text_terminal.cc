#ifndef TEST
#include "output/text_terminal.h"

#include "hardware/io.h"
#include "scheduling/cpu_core.h"
#include "containers/spinlock.h"
#include "common/kernel_string.h"
#include "common/string_view.h"
#include "memory/virtual_allocator.h"

namespace output {

using containers::RecursiveInterruptSafeSpinlock;
using containers::RecursiveInterruptSafeSpinlockGuard;
using hardware::ReadIOByte;
using hardware::WriteIOByte;
using scheduling::GetCurrentCoreId;
using scheduling::kMaxCores;

// The text terminal is implemented by outputting over COM1.
namespace {

// The IO port to use.
constexpr unsigned short kPort = 0x3f8;  // COM1

// Maximum number of spins waiting for UART transmitter holding register to become empty.
constexpr size_t kMaxUartSpins = 100000;

// Charset for hexadecimal digits.
constexpr const char* kHexidecimalCharset = "0123456789ABCDEF";

// Default kernel print source attributes.
constexpr int kKernelPid = 0;
constexpr const char* kKernelName = "Kernel";
constexpr int kDefaultChannel = 0;

// Maximum length of a print source name.
constexpr size_t kMaxSourceNameLength = 80;

// Spinlock protecting serial COM1 port access and terminal emission state.
RecursiveInterruptSafeSpinlock g_serial_print_spinlock;

// Track current active ScopedPrintSource context per core.
ScopedPrintSource* g_current_print_source[kMaxCores] = {nullptr};

// Track last emitted print source attributes.
int g_last_emitted_pid = -1;
char g_last_emitted_name[kMaxSourceNameLength + 1] = {0};
int g_last_emitted_channel = -1;


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
  size_t spins = 0;
  while ((ReadIOByte(kPort + 5) & 0x20) == 0) {
    if (++spins >= kMaxUartSpins) return;
  }
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
  unsigned int uval = static_cast<unsigned int>(val);
  if (val < 0) {
    WriteSerialByte('-');
    uval = 0 - uval;
  }
  if (uval == 0) {
    WriteSerialByte('0');
    return;
  }
  char temp[12];
  int idx = 0;
  while (uval > 0) {
    temp[idx++] = '0' + (uval % 10);
    uval /= 10;
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

// Size of line buffer per core.
constexpr size_t kPrintBufferSize = 256;

struct CorePrintBuffer {
  char buffer[kPrintBufferSize];
  size_t length = 0;
  int pid = kKernelPid;
  char name[kMaxSourceNameLength + 1] = "Kernel";
  int channel = kDefaultChannel;
};

CorePrintBuffer g_print_buffers[kMaxCores];

// Ensures the serial output stream has emitted the current source's control
// sequence.
void EnsurePrintSourceEmitted(int target_pid, const char* target_name,
                              int target_channel) {
  const char* safe_name = target_name ? target_name : kKernelName;
  if (g_last_emitted_pid == target_pid &&
      StringsAreEqual(g_last_emitted_name, safe_name) &&
      g_last_emitted_channel == target_channel)
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
  WriteSerialString(safe_name);
  WriteSerialByte('\007');

  g_last_emitted_pid = target_pid;
  common::CopyString(safe_name, kMaxSourceNameLength, kMaxSourceNameLength,
                     g_last_emitted_name);
  g_last_emitted_channel = target_channel;
}

void FlushCorePrintBuffer(size_t core_id) {
  if (g_print_buffers[core_id].length == 0) return;
  EnsurePrintSourceEmitted(g_print_buffers[core_id].pid,
                           g_print_buffers[core_id].name,
                           g_print_buffers[core_id].channel);
  for (size_t i = 0; i < g_print_buffers[core_id].length; i++)
    WriteSerialByte(g_print_buffers[core_id].buffer[i]);
  g_print_buffers[core_id].length = 0;
}

void AppendCharToCoreBuffer(size_t core_id, char c) {
  int target_pid = kKernelPid;
  const char* target_name = kKernelName;
  int target_channel = kDefaultChannel;
  if (g_current_print_source[core_id] != nullptr) {
    target_pid = g_current_print_source[core_id]->pid();
    target_name = g_current_print_source[core_id]->name();
    target_channel = g_current_print_source[core_id]->channel();
  }

  if (g_print_buffers[core_id].length > 0) {
    if (g_print_buffers[core_id].pid != target_pid ||
        g_print_buffers[core_id].channel != target_channel ||
        !StringsAreEqual(g_print_buffers[core_id].name, target_name)) {
      FlushCorePrintBuffer(core_id);
    }
  }

  if (g_print_buffers[core_id].length == 0) {
    g_print_buffers[core_id].pid = target_pid;
    common::CopyString(target_name ? target_name : kKernelName,
                       kMaxSourceNameLength, kMaxSourceNameLength,
                       g_print_buffers[core_id].name);
    g_print_buffers[core_id].channel = target_channel;
  }

  if (g_print_buffers[core_id].length >= kPrintBufferSize - 1) {
    FlushCorePrintBuffer(core_id);
    g_print_buffers[core_id].pid = target_pid;
    common::CopyString(target_name ? target_name : kKernelName,
                       kMaxSourceNameLength, kMaxSourceNameLength,
                       g_print_buffers[core_id].name);
    g_print_buffers[core_id].channel = target_channel;
  }

  g_print_buffers[core_id].buffer[g_print_buffers[core_id].length++] = c;
  if (c == '\n') FlushCorePrintBuffer(core_id);
}


}  // namespace

ScopedPrintSource::ScopedPrintSource(int pid, const char* name, int channel)
    : pid_(pid), name_(name), channel_(channel) {
  size_t core_id = GetCurrentCoreId();
  previous_source_ = g_current_print_source[core_id];
  g_current_print_source[core_id] = this;
}

ScopedPrintSource::~ScopedPrintSource() {
  size_t core_id = GetCurrentCoreId();
  g_current_print_source[core_id] = previous_source_;
}

Printer::Printer() : number_format_(NumberFormat::Decimal) {}

Printer& Printer::operator<<(char c) {
  RecursiveInterruptSafeSpinlockGuard guard(g_serial_print_spinlock);
  size_t core_id = GetCurrentCoreId();
  if (core_id >= kMaxCores) core_id = 0;
  AppendCharToCoreBuffer(core_id, c);
  return *this;
}

Printer& Printer::operator<<(const char* str) {
  if (str == nullptr) return *this;
  RecursiveInterruptSafeSpinlockGuard guard(g_serial_print_spinlock);
  size_t core_id = GetCurrentCoreId();
  if (core_id >= kMaxCores) core_id = 0;
  while (*str) AppendCharToCoreBuffer(core_id, *str++);
  return *this;
}

Printer& Printer::operator<<(const common::StringView& str) {
  RecursiveInterruptSafeSpinlockGuard guard(g_serial_print_spinlock);
  size_t core_id = GetCurrentCoreId();
  if (core_id >= kMaxCores) core_id = 0;
  for (size_t i = 0; i < str.length; i++)
    AppendCharToCoreBuffer(core_id, str.str[i]);
  return *this;
}

Printer& Printer::operator<<(int c) {
  RecursiveInterruptSafeSpinlockGuard guard(g_serial_print_spinlock);
  if (c < 0) {
    *this << "-";
    *this << static_cast<size_t>(0 - static_cast<unsigned int>(c));
    return *this;
  }
  *this << (size_t)c;
  return *this;
}

Printer& Printer::operator<<(size_t num) {
  RecursiveInterruptSafeSpinlockGuard guard(g_serial_print_spinlock);
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

Printer& Printer::operator<<(NumberFormat format) {
  RecursiveInterruptSafeSpinlockGuard guard(g_serial_print_spinlock);
  number_format_ = format;
  return *this;
}

void Printer::PrintHexidecimal(size_t h) {
  RecursiveInterruptSafeSpinlockGuard guard(g_serial_print_spinlock);
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

void Printer::PrintDecimal(size_t n, bool with_commas) {
  RecursiveInterruptSafeSpinlockGuard guard(g_serial_print_spinlock);
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

}  // namespace output

#endif // TEST
