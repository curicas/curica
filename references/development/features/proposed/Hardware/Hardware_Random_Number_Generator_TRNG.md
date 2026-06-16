# Hardware Random Number Generator (TRNG)

**State**: Proposed
**Difficulty**: Medium
Exposing specialized CPU-level true entropy instructions (e.g., `RDRAND` on x86_64). Bypassing standard OS kernel random pools generates cryptographically secure randomness at massively increased speeds.
