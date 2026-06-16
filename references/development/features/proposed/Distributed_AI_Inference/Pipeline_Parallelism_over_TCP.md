# Pipeline Parallelism over TCP

**State**: Proposed
**Difficulty**: High
Breaking neural network execution graphs into sequential layers and mapping each layer to a different Curica server. Activations are streamed over highly optimized TCP sockets for continuous pipelined inference.
