# Node-API (N-API) Base Integration

**Category**: Interoperability
**Status**: Completed
**Difficulty**: High
To support the massive ecosystem of C++ Node addons (like `bcrypt` or `sqlite3`), Curica provides an expansive C-ABI compatibility layer. It seamlessly intercepts V8 engine API calls (`napi_*`) from compiled `.node` shared libraries and perfectly maps them into Curica's NaN-boxed memory model. This allows unmodified native Node modules to run directly inside Curica with zero wrapper code overhead.
