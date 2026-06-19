# Feature: Virtual Graphics & UI (Framebuffer / WebGPU)

## Overview
To support graphical applications without bundling heavy UI frameworks (GTK, Qt) into the C runtime, Curica will expose a raw virtual framebuffer and hardware-accelerated proxy.

## Requirements
1. **Virtual Framebuffer (`/dev/fb0`):** Expose a shared memory block inside the VFS. WASM processes can compile UI libraries (e.g., Dear ImGui) to write pixel data directly into this buffer.
2. **Host Rendering:** The C runtime simply blits (paints) the contents of the `/dev/fb0` buffer to a lightweight host OS window using the platform's native minimal graphics API (X11/Wayland/Win32/Cocoa).
3. **WebGPU Bridge:** Provide a zero-overhead C-bridge mapping to the WebGPU standard, allowing WASM binaries to perform native hardware-accelerated rendering securely.

## Implementation Details
- Hook the VFS `/dev/fb0` writes to trigger a buffer-swap on the host window system.
- Forward input events (mouse, keyboard) from the host window back to the WASM process via standard IPC or a virtual `/dev/input` character device.
