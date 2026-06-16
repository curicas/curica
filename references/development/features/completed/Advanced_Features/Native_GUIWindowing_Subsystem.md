# Native GUI Windowing Subsystem

**Category**: Advanced_Features
**Status**: Completed

**State**: Approved
**Difficulty**: High
**Architecture**: Localhost WebView Server (Tauri-style)

Expanding Curica beyond headless server environments, the `Curica.WebView` module aims to provide native, cross-platform Desktop GUI windows accessible directly from JavaScript. Instead of relying on a massively bloated framework like Electron (which statically bundles Chromium and V8, adding 150MB+ to binary size), Curica will leverage its ultra-fast C-networking stack to power a lightweight **Localhost WebView Architecture**.

## 1. Architectural Strategy (The Localhost WebView)

The entire GUI backend relies on spawning a tiny OS-native WebView binding (using a single-header C library like `zserge/webview`) while Curica acts as the local backend server. 

### Execution Flow:
1. When a user calls `new WebView.Window()`, Curica spins up an isolated background HTTP and WebSocket server binding to an ephemeral loopback port (e.g., `127.0.0.1:0`), allowing the OS to pick an available high port.
2. The runtime invokes the C-bridge to spawn the native OS WebView. Because we dynamically hook into the pre-installed web engine on the user's host OS (WebView2 on Windows, WebKitGTK on Linux, and WKWebView on macOS), the binary footprint increase is virtually zero (< 500KB).
3. The newly spawned WebView window points its renderer directly to `http://127.0.0.1:<PORT>`.

### The Android/Termux Strategy
Because Cosmopolitan APE binaries natively support ARM64 Linux, Curica runs perfectly on Android within environments like Termux.
To extend GUI support to Android without rewriting the rendering engine, Curica utilizes a fallback mechanism:
- If the runtime detects it is executing in Termux (e.g., via the `PREFIX` environment variable), it skips loading X11/WebKitGTK.
- Instead, it leverages Android Intents via the command line (e.g., `am start -a android.intent.action.VIEW` or the `termux-open-url` utility) to instantly launch the phone's native Chrome browser (or a Custom Tab) pointing to the secure `127.0.0.1` UI port.
- This creates an immediate, app-like mobile GUI experience powered by the exact same `.com` APE binary.

## 2. Security & IPC (Inter-Process Communication) Model

Because the Curica WebView server operates over local TCP sockets rather than Unix Domain Sockets (to ensure compatibility with standard WebViews), security is paramount. We must guarantee that malicious local scripts or other users on the system cannot hijack the UI or send unauthorized commands to the backend.

### Strict Authentication Loop:
- **Ephemeral Bindings**: The backend server is strictly bound to `127.0.0.1`. It immediately drops TCP connections from any other interface.
- **Cryptographic Bearer Tokens**: When starting the HTTP server, Curica generates a random, high-entropy 256-bit authentication token in C.
- **Token Injection**: The OS native window creation command passes this token directly to the WebView (either via an injected initial script or secure headers). 
- **Authenticated Message Bus**: Once the WebView initializes, it opens a secure WebSocket connection back to the server using the token for authentication. 
- **Zero-Trust**: Any HTTP request or WebSocket handshake received by the Curica WebView server that lacks the exact Bearer Token is instantly rejected with a `403 Forbidden`. This securely guarantees that *only* the specific WebView spawned by Curica can communicate with the backend.

## 3. Modular UI & GNOME Default Theming

Since the WebView executes standard HTML/CSS/JS, developers can use any frontend framework they prefer (React, Vue, Svelte). However, Curica provides a completely zero-config, native-feeling UI out of the box through an injected Vanilla CSS module.

### The Curica Component Library
When the WebView loads, Curica automatically injects a highly modular CSS variable architecture into the DOM. This gives developers access to a suite of `<curica-button>`, `<curica-input>`, and `<curica-header>` Web Components.

### GNOME / Adwaita Replication (Default Theme)
To ensure the application feels premium and state-of-the-art on Linux systems, the default theme will precisely replicate the latest **GNOME HIG (Human Interface Guidelines)**.
- **Headerbars**: We replace standard OS title bars with seamless CSS Headerbars (`-webkit-app-region: drag`), allowing toolbars and window controls to merge cleanly.
- **Color Palettes**: Implementation of strict GNOME semantic colors (e.g., `#3584e4` for accents) and deep OS dark-mode synchronization.
- **Typography**: Utilizing `Cantarell` or standard system sans-serif stacks with optimal anti-aliasing.
- **Micro-Animations**: Clean, subtle hover and active states modeled after the GTK4 libadwaita stylesheet.

By combining the lightning-fast Curica backend with a minimal OS WebView and a beautifully styled GNOME component library, developers can build stunning cross-platform desktop applications that compile down to a single `.com` APE binary.


## To look at

https://github.com/slint-ui/slint
https://github.com/slint-ui/slint-cpp-template