# Sensor Data Batching & Compression Engine

**State**: Proposed
**Difficulty**: Medium
A highly specialized background C-routine that continuously reads hardware sensors, batches the data, and compresses it using Zstandard (zstd) before ever alerting the JavaScript execution thread, saving massive CPU cycles.
