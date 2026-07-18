# Makefile for DragonOS

CC = gcc
AS = nasm
LD = ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -std=gnu99 -Isrc
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T src/linker.ld

OBJS = boot.o \
       src/cpu/interrupt.o \
       src/cpu/ports.o \
       src/cpu/gdt.o \
       src/cpu/idt.o \
       src/drivers/serial.o \
       src/drivers/screen.o \
       src/drivers/timer.o \
       src/drivers/keyboard.o \
       src/libc/string.o \
       src/shell/shell.o \
       kernel.o

.PHONY: all clean verify run run-curses run-nographic

all: dragonos.iso

# Pattern rules for nested sources
src/cpu/%.o: src/cpu/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/cpu/%.o: src/cpu/%.asm
	$(AS) $(ASFLAGS) $< -o $@

src/drivers/%.o: src/drivers/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/libc/%.o: src/libc/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/shell/%.o: src/shell/%.c
	$(CC) $(CFLAGS) -c $< -o $@

boot.o: src/boot.asm
	$(AS) $(ASFLAGS) $< -o $@

kernel.o: src/kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

dragonos.bin: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

verify: dragonos.bin
	grub-file --is-x86-multiboot dragonos.bin

dragonos.iso: dragonos.bin grub.cfg
	mkdir -p isodir/boot/grub
	cp dragonos.bin isodir/boot/
	cp grub.cfg isodir/boot/grub/
	grub-mkrescue -o $@ isodir

run: dragonos.iso
	qemu-system-i386 -cdrom dragonos.iso

run-curses: dragonos.iso
	qemu-system-i386 -cdrom dragonos.iso -display curses

run-nographic: dragonos.iso
	qemu-system-i386 -cdrom dragonos.iso -nographic -serial mon:stdio

clean:
	rm -rf $(OBJS) dragonos.bin dragonos.iso isodir
