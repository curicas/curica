# Feature: Declarative JSON Environments

## Overview
Developers will configure their Curica OS environments declaratively using a JSON file (e.g., `curica.env.json`). This file outlines dependencies, required WASM binaries, and workspace mappings.

## Requirements
1. **Schema Parsing**: The runtime must natively parse JSON upon boot.
2. **Environment Definition**: The JSON should support fields for:
   - `packages`: A list of required WASM utility binaries.
   - `env`: Environment variables to set.
   - `entrypoint`: The initial JavaScript "shell script" to execute upon booting.
3. **Population**: The runtime must parse this file before executing the user's entrypoint, mapping the defined configuration directly into the `/home/user` workspace and VFS tree.

## Implementation Details
- Integrate a lightweight JSON parser (such as cJSON) into the runtime C codebase.
- Tie the parsed configuration tree directly to the VFS bootstrapping phase.
