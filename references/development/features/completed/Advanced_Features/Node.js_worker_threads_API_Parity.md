# Node.js `worker_threads` API Parity

**Category**: Advanced_Features
**Status**: Completed
**Difficulty**: Medium
To maximize backwards compatibility with the existing NPM ecosystem, the `worker_threads` global module is being scaffolded. By mapping Node's specific `MessageChannel` and `Worker` class semantics to Curica's underlying POSIX thread pools, legacy server applications utilizing heavy threading can run unmodified.
