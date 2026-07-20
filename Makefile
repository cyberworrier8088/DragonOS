# Makefile for DragonOS (x86_64)

CC = gcc
AS = nasm
LD = ld

CFLAGS = -m64 -ffreestanding -O2 -Wall -Wextra -std=gnu99 -Isrc -mno-red-zone -mno-sse -mno-sse2
ASFLAGS = -f elf64
LDFLAGS = -m elf_x86_64 -T src/linker.ld

# Target-specific CFLAGS for Doom source files to support floating-point returns via SSE
src/doom/%.o: CFLAGS += -DNORMALUNIX -Isrc/doom/doomgeneric -msse -msse2 -mstackrealign -Wno-unused-parameter -Wno-implicit-fallthrough -Wno-missing-field-initializers -Wno-sign-compare -Wno-strict-prototypes -Wno-unused-variable -Wno-unused-but-set-variable -Wno-parentheses
src/libc/stdlib.o: CFLAGS += -msse -msse2 -mstackrealign
src/libc/math.o: CFLAGS += -msse -msse2 -mstackrealign
src/shell/minilua.o: CFLAGS += -Wno-implicit-fallthrough -Wno-missing-field-initializers -Wno-parentheses -Wno-unused-variable -Wno-unused-but-set-variable


DOOM_SRCS = $(filter-out src/doom/doomgeneric/doomgeneric_allegro.c \
                          src/doom/doomgeneric/doomgeneric_emscripten.c \
                          src/doom/doomgeneric/doomgeneric_linuxvt.c \
                          src/doom/doomgeneric/doomgeneric_sdl.c \
                          src/doom/doomgeneric/doomgeneric_soso.c \
                          src/doom/doomgeneric/doomgeneric_sosox.c \
                          src/doom/doomgeneric/doomgeneric_win.c \
                          src/doom/doomgeneric/doomgeneric_xlib.c \
                          src/doom/doomgeneric/i_allegromusic.c \
                          src/doom/doomgeneric/i_allegrosound.c \
                          src/doom/doomgeneric/i_sdlmusic.c \
                          src/doom/doomgeneric/i_sdlsound.c, \
                          $(wildcard src/doom/doomgeneric/*.c))

DOOM_OBJS = $(DOOM_SRCS:.c=.o) src/doom/doomgeneric_dragonos.o

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
       src/libc/stdlib.o \
       src/libc/stdio.o \
       src/libc/math.o \
       src/mm/pmm.o \
       src/mm/kheap.o \
       src/mm/paging.o \
       src/drivers/pci.o \
       src/drivers/rtc.o \
       src/drivers/ata.o \
       src/fs/vfs.o \
       $(DOOM_OBJS) \
       src/shell/shell.o \
       src/shell/gui.o \
       src/shell/minilua.o \
       src/shell/minitcc.o \
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

src/fs/%.o: src/fs/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/doom/%.o: src/doom/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/doom/doomgeneric/%.o: src/doom/doomgeneric/%.c
	$(CC) $(CFLAGS) -c $< -o $@

boot.o: src/boot.asm
	$(AS) $(ASFLAGS) $< -o $@

kernel.o: src/kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

dragonos.bin: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

verify: dragonos.bin
	readelf -h dragonos.bin

limine-bin/limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v8.7.0-binary --depth=1 limine-bin
	$(MAKE) -C limine-bin

isodir/boot/doom1.wad:
	mkdir -p isodir/boot
	curl -L "https://github.com/Akbar30Bill/DOOM_wads/raw/master/doom1.wad" -o isodir/boot/doom1.wad || wget -O isodir/boot/doom1.wad "https://github.com/Akbar30Bill/DOOM_wads/raw/master/doom1.wad"


dragonos.iso: dragonos.bin limine.conf limine-bin/limine isodir/boot/doom1.wad
	mkdir -p isodir/boot
	cp dragonos.bin isodir/boot/
	cp limine.conf isodir/boot/
	cp limine-bin/limine-bios.sys isodir/boot/
	cp limine-bin/limine-bios-cd.bin isodir/boot/
	cp limine-bin/limine-uefi-cd.bin isodir/boot/
	cp wallpaper.bmp isodir/boot/
	cp test.lua isodir/boot/
	cp test.c isodir/boot/
	xorriso -as mkisofs \
		-b boot/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		-o $@ isodir
	./limine-bin/limine bios-install $@

run: dragonos.iso
	qemu-system-x86_64 -m 2G -cdrom dragonos.iso

run-curses: dragonos.iso
	qemu-system-x86_64 -m 2G -cdrom dragonos.iso -display curses

run-nographic: dragonos.iso
	qemu-system-x86_64 -m 2G -cdrom dragonos.iso -nographic -serial mon:stdio

clean:
	rm -rf $(OBJS) dragonos.bin dragonos.iso isodir
