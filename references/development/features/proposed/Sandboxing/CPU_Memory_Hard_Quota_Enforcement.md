# CPU & Memory Hard Quota Enforcement

**State**: Proposed
**Difficulty**: High
Expanding the execution loop to physically count bytecode instructions executed and bytes allocated per-isolate. Instantly kills worker threads that exceed strict latency quotas (e.g., >50ms execution time) or memory footprints.
