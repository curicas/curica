# Zero-Overhead C++ FFI Bridge

**State**: Proposed
**Difficulty**: High
Expanding the existing dlopen bridge to automatically demangle C++ classes. By natively mapping C++ vtable virtual functions to JS prototype chains, developers can instantiate C++ objects without writing N-API wrappers.
