# Watchdog Timer Native Controls

**State**: Proposed
**Difficulty**: Low
A native `Curica.hardware.watchdog` API that requires the JS event loop to continually pet a hardware timer. If the VM hangs due to an infinite loop, the hardware automatically resets the device.
