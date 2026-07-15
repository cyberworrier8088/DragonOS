# DragonOS: Aether GUI & User Experience Specification

This document details the design system, rendering pipeline, personalization options, and input subsystems of the DragonOS user interface.

---

## 1. The Aether Design Language

The GUI of DragonOS is built upon **Aether**, a design language that combines modern visual aesthetics with physical motion rules. Aether focuses on transparency, depth, lighting, and tactile response.

### 1.1 GPU-Accelerated Physical Rendering Engine
Unlike traditional desktop environments that render window components on the CPU and composition on the GPU, Aether’s entire UI pipeline is written as Vulkan shaders:
* **Frosted-Glass Refraction (Glassmorphism)**: Windows are rendered as semi-transparent panels with real-time dynamic refraction. The compositor samples pixels behind a window, applies a dual-cavity Gaussian blur, and shifts colors slightly to simulate thick physical glass.
* **Spring-Physics Motion**: Window movement, snapping, and minimizations use spring formulas rather than simple linear bezier curves. If a user drags a window and lets go, it glides to its snap point with physical inertia and a subtle bounce.
* **Dynamic Illumination**: The active window projects a soft glow onto the desktop and adjacent windows. This light changes color dynamically based on the dominant tone of the content inside the active window.

---

## 2. Personalization & Desktop Customization

Aether makes customization intuitive and lightweight. All settings are stored in human-readable files, but configured through a beautiful graphical Control Panel.

### 2.1 Themes & Accent Layers
DragonOS offers deep styling capabilities:
* **Dual Accent Modes**: Smooth transition between Dark, Light, and *Solarized* modes. Transitions fade the color temperature gradually over 300ms to reduce eye strain.
* **Visual Profile Variables**: Users can toggle corner rounding (from sharp 90° angles to rounded 20px corners), border width (1px to 4px), and the blur intensity of glass panels.

### 2.2 Advanced Window Snapping: Aether Grids
Windows users are familiar with 50/50 snapping. DragonOS expands this into **Aether Grids**:
1. **Interactive Layouts**: Holding `Shift` while dragging a window reveals an overlay showing layout templates (such as 3-column, developer stacks, or presentation grids).
2. **Keyboard Snapping**: Users can use `Super + Arrow Keys` to move windows, or `Super + Number Keys` to place windows into specific zones of custom grids.
3. **Workspace Groups**: When two or more windows are snapped together, the taskbar groups them. Clicking the group icon restores the entire snapped layout instantly.

---

## 3. The Smart Taskbar & Draco Launcher

The center of user interaction in DragonOS is the taskbar and the Start Menu replacement, the **Draco Launcher**.

```
+--------------------------------------------------------------+
| [ Draco ]  [ App 1 ]  [ App 2 ]  [ App 3 (Active) ]  [ 12:45] |
+--------------------------------------------------------------+
     |
     v (Clicking Draco Launcher opens the main menu)
+--------------------------------------------------------------+
|  Omni-Search: [ Search files, settings, web, or ask Draco... ] |
|                                                              |
|  [ Pinned Apps ]               [ Dynamic Flow (AI Predicted)]|
|  - File Explorer               - Visual Studio Code          |
|  - Web Browser                 - Terminal (Git active)       |
|  - Settings                    - Spotify (Afternoon playlist)|
|                                                              |
|  [ Live Widgets: Calendar | CPU Temp | Local Weather ]       |
+--------------------------------------------------------------+
```

### 3.1 Smart Taskbar
* **Dynamic Expansion**: The taskbar shrinks when no apps are open, appearing as a compact dock. As apps launch, it expands to display them. If a window is maximized, the taskbar becomes completely transparent, leaving only the window border visible.
* **Interactive Live Previews**: Hovering over a taskbar icon shows a fully interactive thumbnail. You can scroll through a webpage, pause a video, or type a quick reply to a chat directly in the thumbnail preview without bringing the app to the front.

### 3.2 Draco Launcher & Omni-Search
* **Unified Input**: The search box queries:
  * Application paths and system settings.
  * Local files (indexed instantly by DragonFS).
  * Web resources.
  * Direct system commands (e.g., typing "restart in 10 mins" executes it).
* **AI-Assisted Dynamic Flow**: The launcher does not show a static list. It learns your behavior. In the morning, it highlights your IDE and Slack. In the evening, it suggests games or streaming apps.

---

## 4. Input & Display Pipelines

DragonOS is engineered from the ground up to support modern display hardware and high-precision inputs.

### 4.1 Fractional Vector Scaling
Aether renders everything using vector paths. It does not scale bitmap assets, avoiding the blurriness that Windows displays experience at non-integer scaling ratios (like 125% or 175%). UI elements are sharp on a 4K monitor and a 1080p panel alike.

### 4.2 Seamless Multi-Monitor Control
* **Per-Display Workspace Coordinates**: Moving a window across screens with differing DPI targets triggers a dynamic vector resize on the boundary, keeping the window's physical size consistent.
* **Independently Spanning Desktops**: Virtual desktops can be switched on Monitor 1 while Monitor 2 remains locked to a static document workspace.

### 4.3 High-Priority Input Queue (Aether Touch)
Touch screen and digital pen coordinates bypass the standard OS event loop. They are handled by a dedicated real-time kernel thread.
* **Predictive Ink**: DragonOS runs a lightweight neural net that predicts pen stroke vector paths, dropping pen-to-screen latency to sub-1ms on supported hardware.
* **Natural Gestures**: Standardized 3-finger and 4-finger gestures let users swipe between virtual desktops, pinch to show all open windows, and flip apps.
