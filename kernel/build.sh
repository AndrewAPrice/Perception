#!/bin/bash
ASM_CMD="nasm -felf64 -o"
CC_CMD="gcc -m64 -mcmodel=kernel -ffreestanding -nostdlib -mno-red-zone -c -o"

for f in ./*.asm
do
	echo $f
	$ASM_CMD $f.o $f
done

for f in ./*.c
do
	echo $f
	$CC_CMD $f.o $f
done

ld -nodefaultlibs -T linker.ld -Map=map.txt -o ../fs/boot/kernel.sys *.o
rm *.o
