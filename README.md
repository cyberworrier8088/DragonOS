# DragonOS

DragonOS is a monolithic, 64-bit x86_64 operating system kernel featuring a Windows 7-style Aero graphical user interface (GUI) designed to run on PC-compatible hardware. It boots natively using the **Limine Boot Protocol**, which guarantees a true high-resolution linear graphics framebuffer on both BIOS and UEFI systems.

The kernel is linked to the higher-half virtual address space (`0xffffffff80000000`). During bootstrap, the bootloader transitions the system to 64-bit mode, sets up the virtual page tables mapping the kernel, and passes details of the active graphics display. The bootstrap loader loads a custom GDT (reloading segment selectors to `0x08` for code and `0x10` for data), configures the 64-bit IDT to handle CPU exceptions and IRQs, initializes the double-buffered graphics context, and enters the desktop shell draw loop.

## Architecture and Source Layout

The project source files are organized into modular domains:

*   **src/boot.asm**: 64-bit assembly bootstrap code. Loads a custom GDT, reloads the Code Segment (CS) via a `retfq` far return to `0x08`, loads data segments (DS, ES, SS, FS, GS) with selector `0x10`, and calls `kernel_main`.
*   **src/linker.ld**: Linker script linking the kernel at higher-half address base `0xffffffff80000000`. Allocates text, read-only data, writable data, and uninitialized stack (BSS), and places the `.limine_requests` section in read-only segments.
*   **src/kernel.c**: Main C execution entry point. Declares Limine request structures, fetches active framebuffer details, initializes hardware drivers, and loops the graphical desktop refresh.
*   **src/cpu/**: Core CPU instruction abstractions and hardware descriptor tables:
    *   *ports.c / ports.h*: Low-level port input/output assembly wrappers (`inb`, `outb`, `inw`, `outw`).
    *   *idt.c / idt.h*: 64-bit Interrupt Descriptor Table (16-byte gate entries) and Interrupt Service Routine (ISR) callbacks. When exceptions occur, prints CPU vector, error code, faulting RIP, and CR2 register values to serial.
    *   *interrupt.asm*: Low-level assembly wrappers saving 64-bit registers, passing pointers via `rdi` (System V AMD64 ABI), and performing the `iretq` sequence.
*   **src/drivers/**: Core hardware interfaces:
    *   *graphics.c / graphics.h*: VBE linear framebuffer driver supporting dynamic resolutions up to `1280x1024`. Features shape drawing (pixels, outlines, rectangles, circles), alpha-blended transparency, and back-to-front double-buffer blitting.
    *   *mouse.c / mouse.h*: PS/2 auxiliary mouse driver. Processes 3-byte packets on IRQ12 to track displacement coordinates and left/right clicks.
    *   *serial.c / serial.h*: UART COM1 serial interface operating at 38400 baud, used to stream debug and diagnostic logs.
    *   *timer.c / timer.h*: Programmable Interval Timer (PIT) driver calibrated to trigger IRQ0 at 100Hz.
    *   *keyboard.c / keyboard.h*: Keyboard driver translating scan codes into ASCII characters and routing keypresses to active GUI windows.
*   **src/libc/**: Freestanding C runtime:
    *   *font.h*: Embedded 8x16 VGA bitmap font.
    *   *string.c / string.h*: String helper functions (`strlen`, `strcmp`, `strncmp`, `strcpy`, `strcat`, `reverse`, `int_to_ascii`, `memset`, `memcpy`).
*   **src/shell/**: Windows 7 Aero GUI:
    *   *gui.c / gui.h*: Window manager and desktop shell. Automatically scales positions of the translucent taskbar, clock, tray signal strength meters, speaker icon, and Show Desktop sliver relative to the booted screen resolution. Manages mouse focus/drag states for 4 embedded applications:
        1.  **Computer Info**: Displays CPU registers, memory size, and OS mode.
        2.  **Command Terminal**: Interactive shell running standard commands. Contains a blinking underline text cursor.
        3.  **Calculator**: Styled silver layout with blue-tinted digit buttons performing arithmetic operations.
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
2.  **Verify ELF Header**: Validates the binary compiled format:
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
*   **Start Menu**: Clicking the blue Start Orb toggles the Windows 7 Start Menu. Clicking "Shutdown" safely halts QEMU. Contains a search bar placeholder and smiley profile avatar.
*   **Taskbar Apps**: Glassy square buttons display active windows. Clicking them focuses or minimizes the corresponding window.
*   **Show Desktop**: Clicking the rectangular glassy sliver on the very right of the taskbar minimizes all open windows.
*   **Command Terminal**: Focused input intercepts keystrokes to execute commands: `help`, `about`, `clear`, `ticks`, `ping`, `echo`, `reboot`, and `halt`.
*   **Calculator**: Interactive digit and symbol buttons evaluate integer addition, subtraction, multiplication, and division.
