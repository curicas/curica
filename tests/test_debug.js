console.log("globalThis exists?");
if (globalThis) {
    console.log("Yes, globalThis exists!");
} else {
    console.log("No, globalThis is falsy.");
}

console.log("fetch exists?");
if (globalThis.fetch) {
    console.log("Yes, globalThis.fetch is:", globalThis.fetch);
} else {
    console.log("No, globalThis.fetch is falsy.");
}
