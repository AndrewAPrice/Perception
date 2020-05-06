#pragma once
#include "types.h"

/* this is a VGA driver that is used if the bootloader did not give us a framebuffer,
   it is really low resolution (320x200 256 colours), but that keeps it simplified
   because it's really only a fallback driver to get a useable system */

struct PCIDevice;

extern void init_vga(struct PCIDevice *device);