# DragonOS

DragonOS is a monolithic, 64-bit x86_64 operating system kernel featuring a Windows 7-style Aero graphical user interface (GUI) designed to run on PC-compatible hardware. It is built as a freestanding application conforming to the Multiboot 1 specification, booting via the Limine bootloader.

The operating system bootstraps in 32-bit mode, sets up page directories mapping the first 4 GiB identity mapped, transitions to 64-bit long mode, re-routes PIC hardware interrupts to 64-bit IDT handlers, initializes a 32-bit linear VBE graphics framebuffer, mounts a PS/2 mouse pointer driver, and loops a multi-window graphical desktop manager.

## Architecture and Source Layout

The project source files are organized into modular domains:

*   **src/boot.asm**: Assembly bootstrap code containing the Multiboot header with VBE graphics mode requests, 64-bit GDT, page directory definitions mapping 4 GiB of memory, long mode switch sequence, stack space reservation (16 KiB), and parameter forwarding to `kernel_main`.
*   **src/linker.ld**: Linker script specifying physical memory section layout. It positions the bootloader header and kernel code starting at the 1 MiB boundary.
*   **src/kernel.c**: Main C execution entry point, orchestrating hardware setup, memory layout detection, driver mounting, and the GUI drawing loop.
*   **src/cpu/**: Core CPU instruction abstractions and hardware descriptor tables:
    *   *ports.c / ports.h*: Low-level port input/output assembly wrappers (`inb`, `outb`, `inw`, `outw`).
    *   *idt.c / idt.h*: 64-bit Interrupt Descriptor Table (16-byte gate entries) and Interrupt Service Routine (ISR) callbacks.
    *   *interrupt.asm*: Low-level assembly wrappers saving 64-bit registers, passing pointers via `rdi` (System V AMD64 ABI), and performing the `iretq` sequence.
*   **src/drivers/**: Core hardware interfaces:
    *   *graphics.c / graphics.h*: VBE linear framebuffer driver with drawing primitives (pixels, rectangles, outlines, gradients, circles), alpha-blended transparency, and back-to-front double-buffer blitting.
    *   *mouse.c / mouse.h*: PS/2 auxiliary mouse driver. Processes 3-byte packets on IRQ12 to track displacement coordinates and left/right clicks.
    *   *serial.c / serial.h*: UART COM1 serial interface operating at 38400 baud, used to stream debug output.
    *   *timer.c / timer.h*: Programmable Interval Timer (PIT) driver calibrated to trigger IRQ0 at 100Hz.
    *   *keyboard.c / keyboard.h*: Keyboard driver translating scan codes into ASCII characters and routing keypresses to active GUI windows.
*   **src/libc/**: Freestanding C runtime:
    *   *font.h*: Embedded 8x16 VGA bitmap font.
    *   *string.c / string.h*: String helper functions (`strlen`, `strcmp`, `strncmp`, `strcpy`, `strcat`, `reverse`, `int_to_ascii`, `memset`, `memcpy`).
*   **src/shell/**: Windows 7 Aero GUI:
    *   *gui.c / gui.h*: Window manager and desktop shell. Draws translucent glass taskbar, Start menu panel with shutdown hooks, desktop icons, and manages mouse focus/drag states for 4 embedded applications:
        1.  **Computer Info**: Displays CPU registers, memory size, and OS mode.
        2.  **Command Terminal**: Interactive shell running standard commands.
        3.  **Calculator**: Grid of click buttons performing arithmetic operations.
        4.  **System Monitor**: Neon green scrolling line graph of CPU load history.

## Building and Compilation

To compile DragonOS, you need a standard x86_64 GCC toolchain (native inside standard Linux environments, including Ubuntu/Debian under Windows Subsystem for Linux (WSL)).

### Prerequisites

Install the required utilities:

```bash
sudo apt update
sudo apt install build-essential nasm git xorriso qemu-system-x86
```

### Build Commands

The build system utilizes a Makefile to automate compiling source subdirectories:

1.  **Compile and Link**: Builds the 64-bit kernel binary and wraps it into a bootable ISO image:
    ```bash
    make
    ```
2.  **Verify Multiboot Header**: Validates the binary header format:
    ```bash
    make verify
    ```
3.  **Clean Workspace**: Removes compiled object files, kernel binaries, and ISO files:
    ```bash
    make clean
    ```

## Running the OS

You can launch the compiled ISO in QEMU. Choose the run command suited to your terminal setup:

*   **Standard Interface (GUI)**:
    ```bash
    make run
    ```
*   **Terminal/Text Interface (Curses)**:
    ```bash
    make run-curses
    ```
*   **Headless Console Output (Redirected to Serial)**:
    ```bash
    make run-nographic
    ```

## GUI Interactive Elements

Once DragonOS boots, the following elements are interactive:

*   **Desktop Icons**: Double-clicking or clicking "Computer", "Terminal", "Calc", or "SysMon" spawns or restores the respective window.
*   **Window Titlebar**: Windows can be dragged around the screen by holding the mouse on the blue-glass titlebar. Active windows are brought to the foreground.
*   **Close Button**: Clicking the red 'X' button on the right side of the titlebar closes the window.
*   **Start Menu**: Clicking the blue Start Orb toggles the Windows 7 Start Menu. Clicking "Shutdown" safely halts QEMU.
*   **Command Terminal**: Focused input intercepts keystrokes to execute commands: `help`, `about`, `clear`, `ticks`, `ping`, `echo`, `reboot`, and `halt`.
*   **Calculator**: Interactive digit and symbol buttons evaluate integer addition, subtraction, multiplication, and division.
