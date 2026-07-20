# DragonOS

DragonOS is a monolithic, 64-bit x86_64 operating system kernel with a Windows 11 Fluent-style desktop shell. It boots natively through the **Limine Boot Protocol**, which provides a true high-resolution linear framebuffer on both BIOS and UEFI systems, and it runs complete ports of **Quake 1** and **DOOM** in desktop windows alongside native applications.

The kernel is linked in the higher-half virtual address space (`0xffffffff80000000`). During bootstrap the loader transitions the CPU to 64-bit long mode, installs a custom GDT and 64-bit IDT, switches onto a dedicated 4MB kernel stack, initializes the double-buffered graphics context, and enters the desktop shell loop.

## Feature Summary

*   Limine-booted 64-bit higher-half kernel with SSE/SSE2 enabled for application code
*   Windows 11 Fluent Design desktop: rounded windows, mica-style titlebars, taskbar, start menu, and a global 2D clipping pipeline
*   Quake 1 (shareware) port rendering at 320x240, scaled 2x into a desktop window, with PS/2 keyboard and mouse input
*   DOOM (doomgeneric) port running in a desktop window
*   Native applications: Terminal, Calculator, File Explorer, System Monitor, 2048, Lua interpreter, Tiny C Compiler
*   POSIX-style VFS layer (`open`/`read`/`write`/`lseek`/`close`, `O_CREAT`/`O_TRUNC`/`O_APPEND`, `unlink`)
*   Buddy physical memory allocator, kernel heap with O(1)/O(N) free-block coalescing, 4-level paging
*   Fault isolation: a CPU exception raised inside a game terminates only the game and unwinds back to the desktop rather than halting the system

## Architecture and Source Layout

*   **src/boot.asm**: 64-bit bootstrap. Loads the GDT, reloads segment selectors, switches to a dedicated 4MB kernel stack reserved in BSS, and calls `kernel_main`. The bootloader-provided stack is deliberately abandoned because its size is not guaranteed to accommodate deep C call chains.
*   **src/linker.ld**: Links the kernel at `0xffffffff80000000` and places the Limine request section.
*   **src/kernel.c**: Entry point. Fetches framebuffer details, initializes drivers, registers Limine modules with the VFS, and runs the desktop loop. The loop passes real PIT-measured frame times to the Quake host and yields with `hlt` between game frames; when idle it throttles to approximately 60 FPS.
*   **src/cpu/**: GDT/IDT management, ISR/IRQ assembly stubs, port I/O. The exception handler logs vector, error code, RIP, and CR2 to serial. If the fault originated inside a running game, the handler re-enables interrupts and performs a `longjmp` back to the desktop loop instead of halting.
*   **src/mm/**: Buddy physical page allocator, kernel heap (`kmalloc`/`kfree` with block coalescing), and 4-level paging with TLB invalidation on table updates.
*   **src/drivers/**: Framebuffer graphics (double-buffered, clipped primitives, alpha blending), PS/2 keyboard and mouse (IRQ1/IRQ12), PIT timer at 100Hz, UART serial, RTC, PCI enumeration, ATA.
*   **src/fs/**: In-memory POSIX-style VFS. Limine boot modules (game data, wallpaper, scripts) are registered as read-only files; dynamically created files are heap-backed.
*   **src/libc/**: Freestanding C runtime subset: string/memory routines, `printf` family, `setjmp`/`longjmp`, math functions, and a stdio layer that maps `FILE*` operations onto VFS file descriptors.
*   **src/shell/**: Desktop shell, window manager, terminal, and embedded Lua/TCC.
*   **src/quake/**: Quake 1 engine (WinQuake software renderer) built on the quakegeneric interface. `quakegeneric_dragonos.c` implements the platform layer: palette conversion, 2x framebuffer scaling, raw scancode translation, and mouse delta forwarding.
*   **src/doom/**: doomgeneric-based DOOM port with an equivalent platform layer.
*   **src/2048/**: Native 2048 implementation.

## Porting Notes: Running Id Tech Engines in Kernel Space

Two classes of defects had to be resolved to run these engines stably inside a kernel:

1.  **Stack discipline.** The original engines assume a userspace stack of at least 1MB. Functions such as `R_EdgeDrawing` (~115KB of local span buffers), `R_RenderView_` (62.5KB warp buffer), `R_AliasDrawModel` (~60KB vertex arrays), and `COM_LoadPackFile` (~128KB directory buffer) allocate large arrays on the stack. The kernel now provides a dedicated 4MB stack, and the largest offenders were additionally converted to `static` or hunk allocations.
2.  **Unclamped array indices.** The software rasterizer trusts its inputs: `D_PolysetDrawFinalVerts` and `D_PolysetRecursiveTriangle` index span tables with vertex coordinates that can be negative at the screen edge, and the particle system indexes color ramp tables with a float that can be NaN (NaN comparisons are always false, bypassing the original guard, and converting NaN to `int` yields `INT_MIN`). All such sites now bounds-check before indexing, in line with the project rule that all memory operations are bounds-checked.

## Building and Compilation

A standard x86_64 GCC toolchain is required (native Linux or WSL).

### Prerequisites

```bash
sudo apt update
sudo apt install build-essential nasm git xorriso qemu-system-x86
```

### Game Data

*   **doom1.wad**: downloaded automatically by the Makefile on first build.
*   **pak0.pak**: place the Quake shareware `pak0.pak` (from the freely distributable Quake 1.06 shareware release) in the repository root. The Makefile copies it into the ISO. The file is not committed to the repository.

### Build Commands

```bash
make            # compile and produce dragonos.iso
make verify     # inspect the kernel ELF header
make clean      # remove objects, binaries, and the ISO
```

## Running the OS

```bash
make run            # QEMU with a graphical display
make run-curses     # QEMU curses display
make run-nographic  # headless; serial log on stdio
```

## Desktop Usage

*   **Desktop icons / Start menu tiles**: launch System Info, Terminal, Calculator, System Monitor, DOOM, File Explorer, 2048, and Quake 1.
*   **Windows**: draggable by titlebar; close buttons terminate the owning application. Closing the Quake window shuts the game down and returns cleanly to the desktop.
*   **Terminal**: supports `help`, `about`, `clear`, `ticks`, `ping`, `echo`, `touch`, `rm`, `write`, `lua`, `cc`, `reboot`, and `halt`.
*   **Quake 1**: launches into the standard demo loop. Keyboard (WASD, arrows, Ctrl, Space, Escape for the menu) and mouse click input are forwarded to the engine.

## License

See LICENSE. The Quake and DOOM engine sources are GPL-2.0 by id Software; the shareware game data is distributed under its original shareware terms.
