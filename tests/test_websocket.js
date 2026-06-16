// tests/test_websocket.js — WebSocket lifecycle test (WSS)
console.log("TEST: WebSocket");

globalThis.ws = new WebSocket("wss://echo.websocket.org");

globalThis.ws.onopen = function() {
    console.log("  PASS onopen");
    globalThis.ws.send("ping");
};

globalThis.ws.onmessage = function(msg) {
    console.log("  PASS onmessage:", msg);
    globalThis.ws.close();
};

globalThis.ws.onclose = function() {
    console.log("  PASS onclose");
    console.log("TEST: WebSocket PASSED");
};

globalThis.ws.onerror = function() {
    console.log("  FAIL onerror");
};
