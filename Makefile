# Makefile for DragonOS (x86_64)

CC = gcc
AS = nasm
LD = ld

# SSE is enabled kernel-wide: boot.asm turns it on before any C runs, and
# the interrupt stubs fxsave/fxrstor the FPU state. Compiling some objects
# with -mno-sse and others with -msse breaks the x86_64 ABI at every
# float-passing call between them (stack vs XMM0), which corrupted game
# timing. -mno-red-zone stays: interrupts run on the interrupted stack.
CFLAGS = -m64 -ffreestanding -O2 -Wall -Wextra -std=gnu99 -Isrc -mno-red-zone
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

QUAKE_SRCS = $(wildcard src/quake/*.c)
QUAKE_OBJS = $(QUAKE_SRCS:.c=.o)

OBJS = boot.o \
       src/cpu/interrupt.o \
       src/cpu/ports.o \
       src/cpu/gdt.o \
       src/cpu/idt.o \
       src/cpu/scheduler.o \
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
       src/drivers/e1000.o \
       src/fs/vfs.o \
       $(DOOM_OBJS) \
       $(QUAKE_OBJS) \
       src/shell/shell.o \
       src/shell/gui.o \
       src/shell/minilua.o \
       src/shell/minitcc.o \
       src/2048/2048.o \
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

src/2048/%.o: src/2048/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/doom/%.o: src/doom/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/doom/doomgeneric/%.o: src/doom/doomgeneric/%.c
	$(CC) $(CFLAGS) -c $< -o $@

QUAKE_CFLAGS = $(CFLAGS) -msse -msse2 -mstackrealign -Wno-implicit-function-declaration -Wno-unused-variable -Wno-unused-parameter \
	-Dtimelimit=q_timelimit -Ddeathmatch=q_deathmatch -Dmouse_y=q_mouse_y -Dmouse_x=q_mouse_x -Dstartepisode=q_startepisode \
	-DM_Init=q_M_Init -Dnomonsters=q_nomonsters -DR_InitTextures=q_R_InitTextures -DR_Init=q_R_Init -DR_SetupFrame=q_R_SetupFrame \
	-DR_DrawSprite=q_R_DrawSprite -DWritePCXfile=q_WritePCXfile -DS_Init=q_S_Init -DS_Shutdown=q_S_Shutdown -DS_StartSound=q_S_StartSound \
	-DS_StopSound=q_S_StopSound -Donground=q_onground -Dgammatable=q_gammatable -DV_Init=q_V_Init -DZ_ClearZone=q_Z_ClearZone \
	-DZ_Free=q_Z_Free -Dmainzone=q_mainzone -DZ_CheckHeap=q_Z_CheckHeap -DZ_Malloc=q_Z_Malloc

src/quake/%.o: src/quake/%.c
	$(CC) $(QUAKE_CFLAGS) -c $< -o $@

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

isodir/boot/pak0.pak: pak0.pak
	mkdir -p isodir/boot
	cp pak0.pak isodir/boot/pak0.pak


dragonos.iso: dragonos.bin limine.conf limine-bin/limine isodir/boot/doom1.wad isodir/boot/pak0.pak
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
	qemu-system-x86_64 -m 512M -cdrom dragonos.iso

run-curses: dragonos.iso
	qemu-system-x86_64 -m 512M -cdrom dragonos.iso -display curses

run-nographic: dragonos.iso
	qemu-system-x86_64 -m 512M -cdrom dragonos.iso -nographic -serial mon:stdio

clean:
	rm -rf $(OBJS) dragonos.bin dragonos.iso isodir
