# Feature: Native Cryptography Bridge (WebCrypto)

## Overview
Compiling cryptographic algorithms directly into user-space WebAssembly or running them in JavaScript adds extreme binary bloat and severely degrades performance. The Curica Runtime will solve this by providing a thin, high-performance bridge to the host's native cryptography.

## Requirements
1. **Zero-Bloat Target:** Prevent developers from needing to bundle OpenSSL, libsodium, or similar heavy libraries into their WASM applications.
2. **API Standardization:** Expose the cryptographic primitives via the standard ECMAScript `crypto.subtle` API.
3. **Hardware Acceleration:** The C bridge should map directly to the host OS's native hardware-accelerated APIs (e.g., CNG on Windows, CommonCrypto on macOS, or the lightweight embedded equivalent) ensuring maximum throughput.
4. **Supported Primitives:** AES-GCM, SHA-256/512, RSA-OAEP, HMAC, and secure random byte generation.

## Implementation Details
- Build a lightweight crypto wrapper in `src/builtins.c` or a dedicated `crypto_module.c`.
- Link against a minimalistic, statically-compilable C cryptography library (like Mbed TLS or bearssl) to guarantee cross-platform APE portability without expanding the runtime footprint drastically.
