# Makefile for DragonOS (x86_64)

CC = gcc
AS = nasm
LD = ld

CFLAGS = -m64 -ffreestanding -O2 -Wall -Wextra -std=gnu99 -Isrc -mno-red-zone -mno-sse -mno-sse2
ASFLAGS = -f elf64
LDFLAGS = -m elf_x86_64 -T src/linker.ld

OBJS = boot.o \
       src/cpu/interrupt.o \
       src/cpu/ports.o \
       src/cpu/idt.o \
       src/drivers/serial.o \
       src/drivers/screen.o \
       src/drivers/graphics.o \
       src/drivers/mouse.o \
       src/drivers/timer.o \
       src/drivers/keyboard.o \
       src/libc/string.o \
       src/shell/shell.o \
       src/shell/gui.o \
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

limine-bin/limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v8.7.0-binary --depth=1 limine-bin
	$(MAKE) -C limine-bin

dragonos.iso: dragonos.bin limine.conf limine-bin/limine
	mkdir -p isodir/boot
	cp dragonos.bin isodir/boot/
	cp limine.conf isodir/boot/
	cp limine-bin/limine-bios.sys isodir/boot/
	cp limine-bin/limine-bios-cd.bin isodir/boot/
	xorriso -as mkisofs \
		-b boot/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-o $@ isodir
	./limine-bin/limine bios-install $@

run: dragonos.iso
	qemu-system-x86_64 -cdrom dragonos.iso

run-curses: dragonos.iso
	qemu-system-x86_64 -cdrom dragonos.iso -display curses

run-nographic: dragonos.iso
	qemu-system-x86_64 -cdrom dragonos.iso -nographic -serial mon:stdio

clean:
	rm -rf $(OBJS) dragonos.bin dragonos.iso isodir
