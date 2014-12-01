#pragma once
#include "types.h"

extern void memcpy(unsigned char *dest, const unsigned char *src, size_t count);
extern void memset(unsigned char *dest, unsigned char val, size_t count);
extern bool strcmp(void *a, void *b, size_t count);
extern size_t strlen(const char *str);
extern uint8 inportb (unsigned short _port);
extern void outportb (unsigned short _port, unsigned char _data);
extern int8 inportsb (unsigned short _port);
extern uint16 inportw (unsigned short _port);
extern void outportw (unsigned short _port, uint16 _data);
extern uint32 inportdw (unsigned short _port);
extern void outportdw (unsigned short _port, uint32 _data);
