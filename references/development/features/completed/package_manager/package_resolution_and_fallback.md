# Feature: Package Resolution and Fallback Compilation

## Overview
The runtime must resolve packages defined in the declarative JSON environment securely and deterministically, ensuring that both official developers and regular users experience the same unified logic.

## Requirements
1. **Remote Binary Check**: For a given package dependency, the runtime must first query the official repository (`https://github.com/curicas/curica/tree/main/packages`) via HTTPS.
2. **Local Caching**: If the pre-compiled WASM binary exists remotely, it is downloaded securely to a host-level `packages` cache directory.
3. **Source Compilation Fallback**: If the remote WASM does not exist, the runtime must fallback to fetching the raw source code of the dependency. It must then invoke the embedded toolchain to compile it into WASM locally, saving the result into the host-level `packages` cache.
4. **Bootstrapping the VFS**: Once the WASM is resolved in the local cache, the runtime injects it into the VFS (e.g., `/bin`) so the internal environment can execute it.

## Implementation Details
- Integrate libcurl or native HTTPS socket streams to query the GitHub API/raw URLs.
- Ensure the local `packages` cache is deduplicated using hashes.
- Allow official developers to simply commit their newly compiled local `packages` files to the repository to distribute them globally.
