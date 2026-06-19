# Feature: Native Daemonization & Scheduled Jobs

## Overview
A comprehensive virtual OS requires the ability to run headless services and scheduled tasks without relying on external host daemons like `systemd` or `cron`.

## Requirements
1. **JSON Scheduling:** The declarative `curica.env.json` configuration will support a `jobs` array, allowing developers to define chron expressions for specific WASM/JS entrypoints.
2. **Headless Execution:** The runtime must support executing completely in the background, detaching from the host terminal.
3. **Efficient Polling:** Scheduled jobs should not keep heavy processes resident in memory. The native Event Loop will simply wake, spawn the required WASM/JS process at the scheduled tick, and clean it up upon completion.

## Implementation Details
- Integrate a lightweight chron-expression parser into the JSON configuration boot sequence.
- Register long-sleeping timers in the C event loop (`src/event_loop.c`) to manage process spawning asynchronously.
