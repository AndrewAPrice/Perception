#!/bin/bash
NASM_CMD="nasm -felf64 -o"
GCC_CMD="gcc -m64 -mcmodel=kernel -ffreestanding -nostdlib -mno-red-zone -c -o"

$GCC_CMD atapi.o atapi.c
$NASM_CMD boot.o boot.asm
#$NASM_CMD gdt_asm.o gdt.asm
# $GCC_CMD gdt.o gdt.c
$GCC_CMD ide.o ide.c
$GCC_CMD idt.o idt.c
$GCC_CMD io.o io.c
$NASM_CMD irq_asm.o irq.asm
$GCC_CMD irq.o irq.c
$NASM_CMD isr_asm.o isr.asm
$GCC_CMD isr.o isr.c
$GCC_CMD keyboard.o keyboard.c
$GCC_CMD liballoc.o liballoc.c
$GCC_CMD liballoc_hooks.o liballoc_hooks.c
$GCC_CMD main.o main.c
$GCC_CMD messages.o messages.c
$NASM_CMD multiboot.o multiboot.asm
$GCC_CMD pci.o pci.c
$GCC_CMD physical_allocator.o physical_allocator.c
$GCC_CMD process.o process.c
$GCC_CMD text_terminal.o text_terminal.c
$GCC_CMD timer.o timer.c
# $GCC_CMD vfs.o vfs.c
$GCC_CMD virtual_allocator.o virtual_allocator.c
ld -nodefaultlibs -T linker.ld -Map map.txt -o ../fs/boot/kernel.sys *.o
rm *.o
