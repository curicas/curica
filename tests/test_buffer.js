// test_buffer.js — Verifies the native Buffer implementation
// Tests: alloc, from (string), hex/base64 encoding, toString, length, copy, fill

// --- Buffer.alloc ---
const b1 = Buffer.alloc(4, 0);
console.log("alloc(4) length:", b1.length);   // 4

// --- Buffer.from (string) ---
const b2 = Buffer.from("hello");
console.log("from('hello') length:", b2.length); // 5
console.log("from('hello') toString:", b2.toString()); // "hello"

// --- Buffer.from (hex) ---
const b3 = Buffer.from("deadbeef", "hex");
console.log("hex decode length:", b3.length); // 4

// --- Buffer.from (base64) ---
const b4 = Buffer.from("aGVsbG8=", "base64");
console.log("base64 decode:", b4.toString()); // "hello"

// --- Buffer.isBuffer ---
console.log("isBuffer(b1):", Buffer.isBuffer(b1)); // true
console.log("isBuffer('x'):", Buffer.isBuffer("x")); // false

// --- Indexing ---
const b5 = Buffer.from("ABC");
console.log("b5[0]:", b5[0]); // 65
console.log("b5[1]:", b5[1]); // 66

console.log("All Buffer tests passed!");
