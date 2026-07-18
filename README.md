# DragonOS

DragonOS is a monolithic, 32-bit x86 operating system kernel designed to run on IBM PC-compatible hardware. It is built as a freestanding application conforming to the Multiboot 1 specification, booting via the Limine bootloader.

The operating system initializes descriptor tables (GDT and IDT), configures hardware interrupt vectors via remapping the PIC, installs device drivers for standard console components, and runs an interactive command-line shell.

## Architecture and Source Layout

The project source files are organized into modular domains:

*   **src/boot.asm**: Assembly bootstrap code containing the Multiboot header, stack space reservation (16 KiB), segment initialization, and the jump to `kernel_main`.
*   **src/linker.ld**: Linker script specifying physical memory section layout. It positions the bootloader header and kernel code starting at the 1 MiB boundary.
*   **src/kernel.c**: Main C execution entry point, orchestrating hardware setup and the interactive shell loop.
*   **src/cpu/**: Core CPU instruction abstractions and hardware descriptor tables:
    *   *ports.c / ports.h*: Low-level port input/output assembly wrappers (`inb`, `outb`, `inw`, `outw`).
    *   *gdt.c / gdt.h*: Global Descriptor Table configuration defining null, kernel code, kernel data, user code, and user data segments.
    *   *idt.c / idt.h*: Interrupt Descriptor Table and Interrupt Service Routine (ISR) callbacks.
    *   *interrupt.asm*: Low-level assembly wrappers saving registers, re-routing hardware interrupt interrupts (IRQs 0-15), and performing the `iret` sequence.
*   **src/drivers/**: Core hardware interfaces:
    *   *screen.c / screen.h*: VGA text mode terminal driver with scrolling support, backspace support, and hardware cursor updating via I/O ports.
    *   *serial.c / serial.h*: UART COM1 serial interface operating at 38400 baud, used to stream debug output.
    *   *timer.c / timer.h*: Programmable Interval Timer (PIT) driver calibrated to trigger IRQ0 at 100Hz.
    *   *keyboard.c / keyboard.h*: Keyboard driver translating scan codes into ASCII characters, tracking standard Shift and Caps Lock layout states.
*   **src/libc/**: Freestanding C runtime:
    *   *string.c / string.h*: String helper functions (`strlen`, `strcmp`, `strncmp`, `reverse`, `int_to_ascii`, `memset`, `memcpy`, `append`, `backspace`).
*   **src/shell/**: Interactive CLI:
    *   *shell.c / shell.h*: Custom command processor executing commands entered by the keyboard or outputted via VGA and serial.

## Building and Compilation

To compile DragonOS, you need an x86 cross-compiler or a Linux environment with 32-bit development libraries installed (e.g., Ubuntu/Debian under Windows Subsystem for Linux (WSL)).

### Prerequisites

Install the required utilities:

```bash
sudo apt update
sudo apt install build-essential nasm git xorriso qemu-system-x86
```

### Build Commands

The build system utilizes a Makefile to automate compiling source subdirectories:

1.  **Compile and Link**: Builds the kernel binary and wraps it into a bootable ISO image:
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

## Shell Commands

Once DragonOS boots, you are presented with the `dragonos>` prompt. The following commands are supported:

*   **help**: Lists all available commands.
*   **about**: Displays detailed kernel metadata, layout, and architecture versions. (Bootloader: Limine)
*   **clear**: Clears the console screen and repositions the cursor.
*   **ticks**: Prints the total number of hardware timer ticks elapsed since system boot.
*   **ping**: Simple diagnostic command which prints "pong!".
*   **echo [text]**: Prints the text directly back to the terminal screen.
*   **serial [text]**: Writes the text to the COM1 serial interface.
*   **reboot**: Reboots the machine by writing to the keyboard controller command register (port 0x64).
*   **halt**: Halts the CPU safely.
