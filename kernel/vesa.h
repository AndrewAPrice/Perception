#pragma once
#include "types.h"

struct PCIDevice;
struct multiboot_tag;

extern void init_vesa(struct PCIDevice *device);
extern void handle_vesa_multiboot_header(struct multiboot_tag *tag);