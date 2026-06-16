# CPU Flamegraph Generator

**State**: Proposed
**Difficulty**: High
Exporting `perf`-compatible instruction traces. The VM continuously samples the CallFrame stack, mapping C-dispatcher cycles back to JS source lines, instantly visualizing bottlenecks as flamegraphs.
