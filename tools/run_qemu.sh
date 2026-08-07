#!/bin/bash
set -e

ISO_PATH="$1"

if [ -z "$ISO_PATH" ]; then
  echo "Usage: $0 <path-to-iso>"
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

qemu-system-x86_64 \
  -display cocoa \
  -boot d \
  -drive id=sata_cd,file="$ISO_PATH",if=none,format=raw \
  -device ich9-ahci,id=ahci \
  -device ide-cd,drive=sata_cd,bus=ahci.0 \
  -device intel-hda \
  -audiodev coreaudio,id=audio0 \
  -device hda-output,audiodev=audio0 \
  -m 2048 \
  -serial stdio \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
  -netdev user,id=net0 \
  -device virtio-net-pci,netdev=net0 \
  -device virtio-tablet-pci \
  -device virtio-mouse-pci \
  -vga virtio | python3 "$SCRIPT_DIR/log_viewer.py"
