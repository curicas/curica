# Feature: Virtual Audio (/dev/dsp)

## Overview
Interactive applications (games, synthesizers) require audio output. Implementing the massive WebAudio API in C is anti-pattern to the microkernel philosophy. Curica will use a classic UNIX approach.

## Requirements
1. **Virtual Audio Device:** Expose a ring buffer in the VFS as a character device: `/dev/dsp` or `/dev/audio`.
2. **Direct PCM Writing:** WASM modules compute raw audio samples (PCM data) and use standard POSIX `write()` calls to pipe the bytes directly into the `/dev/dsp` file descriptor.
3. **Host Blitting:** The C runtime continuously reads from this ring buffer and forwards it to the host OS audio subsystem (ALSA, CoreAudio, WASAPI) using an ultra-lightweight header (e.g., `miniaudio`).

## Implementation Details
- Intercept writes to `/dev/dsp` inside the VFS layer.
- Spin up a high-priority, low-latency background thread in C dedicated solely to draining the ring buffer to the host audio hardware, ensuring pop-free playback without blocking the main event loop.
