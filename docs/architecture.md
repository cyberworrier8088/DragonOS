# DragonOS: Core & Architecture Specification

This document details the underlying kernel, memory management, security model, and backward-compatibility systems of DragonOS.

---

## 1. Hybrid Microkernel Architecture

DragonOS is powered by the **DracoKernel**, an async-first hybrid microkernel written in Rust. It combines the reliability of a microkernel with the raw performance of a monolithic system by isolating subsystems into protected user-space domains while using highly optimized IPC (Inter-Process Communication) channels.

```mermaid
graph TD
    subgraph User Space
        App[Applications / Sandboxes] -->|Direct NT Translation| Wyvern[Wyvern Layer]
        App -->|Native Syscalls| VFS[Virtual File System Server]
        App -->|GPU Draw Calls| GUI[Aether Compositor Server]
        Wyvern --> VFS
        Wyvern --> GUI
    end

    subgraph Kernel Space (DracoKernel - Rust)
        VFS -->|Fast IPC| Micro[Core Microkernel: Scheduler, MMU, IPC]
        GUI -->|Fast IPC| Micro
        
        subgraph Hardware Layer
            Micro --> CPU[Intel/AMD CPU]
            Micro --> NVMe[Direct-to-NVMe Ring]
            Micro --> HSM[Hardware Security Module / TPM 3.0]
        end
    end
```

### 1.1 Asymmetric Multi-Core Scheduling (AeroSched)
Modern CPUs feature heterogeneous architectures (e.g., Performance-cores and Efficient-cores). Traditional schedulers often misallocate resources, causing UI micro-stutters.
* **Core Mapping**: AeroSched maintains separate queues for foreground interactive tasks (GPU compositor, active window) and background tasks (indexing, telemetry, update services).
* **Intel Thread Director / AMD V-Cache Integration**: DracoKernel coordinates directly with hardware execution trace tables to pin latency-critical UI threads to high-frequency P-cores, while background routines are parked on E-cores.
* **Zero-Context-Switch IPC**: Utilizing shared-memory rings for IPC between user-space drivers and the kernel, DragonOS avoids the register-dumping overhead of traditional context switches.

### 1.2 Direct-to-NVMe Pipeline & Huge Pages
* **5-Level Paging**: Full 64-bit virtual address space support handling up to 256 Terabytes of physical RAM.
* **Transparent Huge Pages (THP)**: DragonOS enforces 2MB and 1GB physical memory page allocations for large databases, compilers, and games, reducing TLB (Translation Lookaside Buffer) misses by up to 40%.
* **Lockless Storage Queues**: By passing raw NVMe command rings directly into the Virtual File System (VFS) server, user applications write to NVMe devices with zero kernel-space copying, achieving read speeds that saturated PCIe Gen 5 limits (14+ GB/s).

---

## 2. Strong Security by Default

DragonOS operates on a **Zero-Trust Operating Paradigm**. System access is capability-based, meaning applications have zero implicit access to hardware, system files, or user directories unless explicitly granted.

### 2.1 Hardware-Enforced Sandboxing
* **Dragon Sandboxes (D-Boxes)**: Every third-party app runs inside an isolated D-Box container. Using CPU virtualization extensions (Intel VT-x / AMD-V), the OS boots a lightweight micro-visor around each app. 
* **Capability Tokens**: Instead of filesystems being globally open, an app is handed a virtual file descriptor pointing only to the directory the user selected via the system file picker.

### 2.2 Cryptographic Integrity & Secure Boot
* **Multi-Stage Secure Boot**: During boot, each stage (UEFI -> Bootloader -> DracoKernel -> System Services) cryptographically verifies the SHA-256 hash of the next stage using keys stored in the Hardware Security Module (HSM / TPM 3.0).
* **Immutable Core**: The system root partition is mounted as a read-only, cryptographically signed image. OS updates are applied atomically via A/B partition switching, preventing malware from injecting code into system directories.

### 2.3 Ransomware Rollback (ShadowJournal)
* **Copy-on-Write (CoW) Shadowing**: DragonFS (our native filesystem) maintains light, metadata-only snapshots of the user's home folder.
* **Behavioral Heuristics**: If the kernel detects a high frequency of file writes involving entropy increases (characteristic of encryption algorithms) on user documents, it instantly:
  1. Suspends the offending process tree.
  2. Quarantines the modified files.
  3. Displays a warning to the user.
  4. Offers a single-click rollback to the snapshot taken seconds before the attack.

---

## 3. Memory & Power Management

DragonOS is designed for extreme efficiency across both desktop workstations and ultra-portable laptops.

| Subsystem | Strategy | Impact |
| :--- | :--- | :--- |
| **Active Memory** | **DracoCompress** (LZ4 Hardware Engine) | Inactive pages in RAM are compressed dynamically in-hardware before being written to swap space on NVMe, increasing effective memory capacity by up to 70%. |
| **Idle Power** | **Zero-Power Idle (ZPI)** | Places CPU sockets, PCI lanes, and memory buses into ultra-low power C-states during micro-intervals of inactivity (even between keystrokes). |
| **Background Control** | **Dynamic Suspension** | Laptops automatically freeze CPU cycles for out-of-focus browser tabs and apps, waking them in under 1 millisecond when the user clicks them. |

---

## 4. Wyvern Backward Compatibility Layer

A new operating system is useless without software. DragonOS includes **Wyvern**, a native user-space compatibility layer that runs legacy Windows applications with near-zero overhead.

```
+------------------------------------------+
|  Legacy Windows Application (.exe)       |
+------------------------------------------+
                    |
                    v (Intercepted by Wyvern)
+------------------------------------------+
|  Wine / Proton NTDLL & Win32 Translation |
+------------------------------------------+
                    |
                    v (Direct Map)
+------------------------------------------+
|  Direct3D -> Vulkan Layer (DXVK/vkd3d)   |
+------------------------------------------+
                    |
                    v
+------------------------------------------+
|  DragonOS Kernel / Native GPU Drivers    |
+------------------------------------------+
```

### 4.1 System Call Interception
Wyvern intercepts Win32 and NT system calls at the user-space level and maps them directly to DracoKernel equivalents. Unlike a virtual machine, there is no emulation of CPU instructions—the application code runs directly on the metal.

### 4.2 Dynamic Binary Translation (DBT)
For older 32-bit x86 Windows applications, Wyvern includes a Just-In-Time (JIT) dynamic binary translator that converts x86 instructions into native ARM64 or x86-64 instructions on the fly, with caching to ensure high performance on repeated runs.

### 4.3 Direct3D to Vulkan translation
Gaming is critical for desktop success. DragonOS integrates DXVK (Direct3D 9/10/11 to Vulkan) and vkd3d-proton (Direct3D 12 to Vulkan) directly into the system's display server pipeline. Because Vulkan runs bare-metal on our AMD, Nvidia, and Intel drivers, games run at frame rates matching or exceeding Windows performance.
