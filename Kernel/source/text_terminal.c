#include "text_terminal.h"

#include "io.h"
#include "virtual_allocator.h"

// The text terminal is implemented by outputting over COM1.

// The sIO port to use.
#define PORT 0x3f8  // COM1

// Has the serial output been initialized?
bool serial_output_initialized = false;

// Initialize the serial output.
void InitializeSerialOutput() {
  outportb(PORT + 1, 0x00);  // Disable all interrupts
  outportb(PORT + 3, 0x80);  // Enable DLAB (set baud rate divisor)
  outportb(PORT + 0, 0x03);  // Set divisor to 3 (lo byte) 38400 baud
  outportb(PORT + 1, 0x00);  //                  (hi byte)
  outportb(PORT + 3, 0x03);  // 8 bits, no parity, one stop bit
  outportb(PORT + 2, 0xC7);  // Enable FIFO, clear them, with 14-byte threshold
  outportb(PORT + 4, 0x0B);  // IRQs enabled, RTS/DSR set
  serial_output_initialized = true;
}

// Prints a single character.
void PrintChar(char c) {
  if (!serial_output_initialized) {
    InitializeSerialOutput();
  }
  while ((inportb(PORT + 5) & 0x20) == 0)
    ;
  outportb(PORT, c);
}

// Prints a null-terminated string.
void PrintString(const char *str) {
  char *str1 = (char *)str;
  while (*str1) {
    PrintChar(*str1);
    str1++;
  }
}

// Prints a fixed length string.
void PrintFixedString(const char *str, size_t length) {
  size_t i;
  for (i = 0; i < length; i++) PrintChar(str[i]);
}

// Prints a number as a 64-bit hexidecimal string.
void PrintHex(size_t h) {
  PrintChar('0');
  PrintChar('x');
  const char *charset = "0123456789ABCDEF";
  char temp[16];

  size_t i;
  for (i = 0; i < 16; i++) {
    temp[i] = charset[h % 16];
    h /= 16;
  }

  PrintChar(temp[15]);
  PrintChar(temp[14]);
  PrintChar(temp[13]);
  PrintChar(temp[12]);
  PrintChar('-');
  PrintChar(temp[11]);
  PrintChar(temp[10]);
  PrintChar(temp[9]);
  PrintChar(temp[8]);
  PrintChar('-');
  PrintChar(temp[7]);
  PrintChar(temp[6]);
  PrintChar(temp[5]);
  PrintChar(temp[4]);
  PrintChar('-');
  PrintChar(temp[3]);
  PrintChar(temp[2]);
  PrintChar(temp[1]);
  PrintChar(temp[0]);
}

// Prints a number as a decimal string.
void PrintNumber(size_t n) {
  if (n == 0) {
    PrintChar('0');
    return;
  }

  /* maximum value is 18,446,744,073,709,551,615
                      01123445677891111111111111
                                   0012334566789*/
  char temp[20];
  size_t first_char = 20;

  while (n > 0) {
    first_char--;
    temp[first_char] = '0' + (char)(n % 10);
    n /= 10;
  }

  size_t i;
  for (i = first_char; i < 20; i++) {
    PrintChar(temp[i]);
    if (i == 1 || i == 4 || i == 7 || i == 10 || i == 13 || i == 16)
      PrintChar(',');
  }
}
