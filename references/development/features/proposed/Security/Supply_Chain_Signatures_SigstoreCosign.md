# Supply Chain Signatures (Sigstore/Cosign)

**State**: Proposed
**Difficulty**: High
Baking Sigstore verifications directly into the Curica module loader. The engine refuses to execute downloaded NPM modules or third-party dependencies unless their digital signatures cryptographically verify against the transparency log.
