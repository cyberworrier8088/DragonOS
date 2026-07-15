# DragonOS: Power & Productivity Specification

This document details the integrated AI automation engine, virtual workspace management, next-generation filesystem, and the developer-native tools of DragonOS.

---

## 1. Draco AI: Local-First System Intelligence

Unlike operating systems that bundle external cloud-based AI chat interfaces, DragonOS features **Draco AI**, an intelligence engine running directly on the local NPU (Neural Processing Unit).

```
+-------------------------------------------------------------+
|                      USER NATURAL INPUT                     |
|      "Summarize yesterday's PDF reports and save to doc"    |
+-------------------------------------------------------------+
                               |
                               v (Parsed by Local NPU)
+-------------------------------------------------------------+
|                         DRACO AI                            |
|  1. Queries DragonFS Index for "Date: Yesterday, Type: PDF" |
|  2. Extracts text using local offline OCR/Parser            |
|  3. Synthesizes summaries using local quantized LLM         |
|  4. Invokes VFS Server to write output file                 |
+-------------------------------------------------------------+
                               |
                               v (Executed)
+-------------------------------------------------------------+
|                      COMPLETED WORK                         |
+-------------------------------------------------------------+
```

### 1.1 Privacy-First Architecture
Draco AI requires zero network connection. All inference is processed locally using a highly optimized, quantized 8-billion parameter LLM integrated directly into the system libraries, utilizing less than 2.5W of power during active processing.

### 1.2 System-Level Automation
Draco AI possesses capability tokens that allow it to safely execute tasks across the OS:
* **System Control**: Users can issue commands like *"Draco, turn off notifications, dim the screen, and set a timer for 25 minutes"* or *"Draco, optimize my battery profile for compilation."*
* **Semantic File Querying**: Draco scans and creates semantic embeddings of local documents in the background. Users can search for concepts rather than filenames: *"Find that budget sheet where we discussed software costs"* locates the file even if it is named `Sheet_v2_final.xlsx`.

---

## 2. Workspaces & Keyboard-Centric Window Tiling

Multitasking in DragonOS is designed for high efficiency, appealing to both casual mouse users and keyboard-driven developers.

### 2.1 Workspace Virtual Desktops
* **State Preservation**: Virtual desktops in DragonOS go beyond simple window grouping. A workspace saves the exact layout, open folders, terminal history, and browser sessions.
* **Context Transitions**: Swiping between workspaces instantly suspends the RAM footprint of the inactive workspace and loads the active one, ensuring 100% CPU focus on the task at hand.

### 2.2 Native Hybrid Tiling Window Manager
* **Keyboard Toggle**: Pressing `Super + T` toggles the active workspace into Tiling Mode. Windows are instantly arranged in an optimal non-overlapping grid.
* **Layouts**: Supports standard Master-Stack layouts, Fibonacci spirals, and symmetric splits. 
* **Dynamic Sizing**: Dragging the border of one tiled window dynamically resizes all surrounding windows to fit the new spacing, maintaining clean gaps.

---

## 3. DragonFS: The Next-Gen Filesystem

DragonOS replaces legacy NT/FAT filesystems with **DragonFS**, a transactional, log-structured filesystem designed from the ground up for NVMe storage devices and flash memory.

### 3.1 Architecture & Integrity
* **Zero-Write-Locking**: DragonFS uses a lockless log-structured write queue, ensuring writes scale linearly with the number of CPU cores.
* **Metadata Checksumming**: Every file block and metadata entry is protected by a cryptographic checksum (BLAKE3). If bit-rot occurs, the filesystem automatically repairs the corrupted file block using background parity logs.

### 3.2 Real-time Indexing
* **No Background Scans**: In Windows, file search requires a background indexer that hogs CPU and disk bandwidth. In DragonFS, index keys are inserted directly into a global B-Tree database *during* the file-write system call.
* **Sub-2ms Searches**: Directory listings and system-wide file queries complete in under 2 milliseconds, regardless of drive occupancy.

### 3.3 End-to-End Cryptographic Sync
* **Native Syncing**: Cloud sync is baked into the filesystem driver. Users can select any folder and share it with other DragonOS machines via a secure, end-to-end encrypted peer-to-peer network, eliminating the need for heavy third-party sync apps.

---

## 4. Developer-First Ecosystem

DragonOS is designed to be a developer’s dream environment, offering native Unix-style tools on top of a highly optimized 64-bit kernel.

### 4.1 Dragon Terminal
* **GPU Rendering**: Powered by a Vulkan terminal renderer, text scroll rates easily match 240Hz screen refresh rates with zero latency.
* **Rich Media**: Supports inline display of images, vector files, markdown rendering, and interactive charts natively inside the shell.
* **AI Completion**: Draco AI provides inline shell suggestion overlays, explaining commands and proposing arguments as you type.

### 4.2 The `drg` Package Manager
* **Declarative Configurations**: Applications and packages are configured using declarative manifests. 
* **Sandboxed Installs**: Packages installed via `drg` are isolated in private directories. They cannot write to global system folders, preventing DLL hell and software conflicts.
* **Atomic Rollbacks**: Upgrading an application or compiler toolchain is atomic. If a new version breaks your environment, running `drg rollback` restores the exact binary state in under a second.

### 4.3 Native Container Support
* **Dragon Containers (D-Containers)**: DragonOS includes lightweight container virtualization directly in the kernel. Developers can run OCI-compliant containers (like Docker) natively without running a nested virtual machine.
* **Zero-Overhead Networking**: Container ports map directly to the host's virtual network card, boosting network throughput by 30% compared to virtualization-based solutions on other OSes.
